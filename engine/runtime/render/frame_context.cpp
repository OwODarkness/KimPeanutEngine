#include "frame_context.h"

#include <stdexcept>

#include "graphics/backend/common/render_backend.h"
#include "render_resource_resolver.h"

namespace kpengine::render
{
    namespace
    {
        constexpr size_t kUniformVectorAlignment = 16;

        size_t AlignUp(size_t value, size_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        size_t GetMaterialConstantAlignment(const MaterialParameterValue &value)
        {
            return std::holds_alternative<Vector4f>(value) ? kUniformVectorAlignment : alignof(float);
        }

        size_t GetMaterialConstantSize(const MaterialParameterValue &value)
        {
            return std::holds_alternative<Vector4f>(value) ? sizeof(Vector4f) : sizeof(float);
        }

        bool HasBinding(const std::vector<graphics::ResourceBinding> &bindings,
                        uint32_t binding_index)
        {
            for (const graphics::ResourceBinding &binding : bindings)
            {
                const bool matches = std::visit(
                    [binding_index](const auto &value) { return value.binding == binding_index; },
                    binding);
                if (matches)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void FrameContext::Initialize(graphics::RenderBackend &backend, size_t uniform_capacity)
    {
        backend_ = &backend;
        uniform_alignment_ = backend.GetUniformBufferAlignment();
        if (uniform_alignment_ == 0)
        {
            uniform_alignment_ = 1;
        }
        uniform_capacity_ = AlignUp(uniform_capacity, uniform_alignment_);
        uniform_buffer_ = backend.CreateUniformBuffer(static_cast<uint32_t>(uniform_capacity_));
        uniform_mapped_ = backend.MapUniformBuffer(uniform_buffer_, uniform_capacity_);
        if (!uniform_buffer_.IsValid() || !uniform_mapped_)
        {
            throw std::runtime_error("Failed to initialize frame uniform allocator");
        }
    }

    void FrameContext::Begin(uint32_t frame_index, const FrameGlobals &globals,
                             graphics::Extent2D render_extent)
    {
        if (!backend_ || !uniform_mapped_)
        {
            throw std::runtime_error("FrameContext is not initialized");
        }
        frame_index_ = frame_index;
        globals_ = globals;
        render_extent_ = render_extent;
        ReleaseTransientBindings();
        uniform_cursor_ = 0;
        active_ = true;
    }

    void FrameContext::End()
    {
        active_ = false;
    }

    UniformAllocation FrameContext::AllocateUniform(size_t size)
    {
        if (!active_ || size == 0)
        {
            return {};
        }
        const size_t offset = AlignUp(uniform_cursor_, uniform_alignment_);
        if (offset > uniform_capacity_ || size > uniform_capacity_ - offset)
        {
            return {};
        }
        uniform_cursor_ = offset + size;
        return {uniform_buffer_, offset, size,
                static_cast<uint8_t *>(uniform_mapped_) + offset};
    }

    graphics::DescriptorSetHandle FrameContext::AllocateResourceBindingSet(
        graphics::PipelineHandle pipeline, const graphics::ResourceBindingSetDesc &desc)
    {
        if (!active_ || !backend_ || !pipeline.IsValid())
        {
            return {};
        }
        const graphics::DescriptorSetHandle handle =
            backend_->CreateResourceBindingSet(pipeline, desc);
        if (handle.IsValid())
        {
            transient_binding_sets_.push_back(handle);
        }
        return handle;
    }

    FrameMaterialBinding FrameContext::CreateMaterialBinding(
        const MaterialSystem &materials, const RenderResourceResolver &resolver,
        MaterialInstanceHandle material_instance,
        const std::vector<graphics::ResourceBinding> &draw_bindings)
    {
        if (!active_ ||
            materials.GetInstanceResolution(material_instance).state != MaterialResourceState::Ready)
        {
            return {};
        }

        const MaterialTemplateHandle template_handle = materials.GetInstanceTemplate(material_instance);
        const MaterialTemplateDesc *const material_template = materials.FindTemplate(template_handle);
        const graphics::PipelineHandle pipeline = resolver.FindMaterialPipeline(template_handle);
        const auto *const textures = resolver.FindTextureBindings(material_instance);
        if (!material_template || !pipeline.IsValid() || !textures)
        {
            return {};
        }

        std::vector<graphics::ResourceBinding> bindings = draw_bindings;
        const bool uses_bindless_textures = textures->uses_bindless_textures;
        if (HasBinding(bindings, kMaterialConstantsBinding))
        {
            return {};
        }

        // V1 compatible shaders read uvec4 texture_indices[] from the start
        // of the binding-3 material block; parameter ID is the array index.
        // std140 gives each uvec4 a 16-byte stride, so the CPU layout is
        // explicit and backend-neutral.
        const size_t bindless_index_bytes = uses_bindless_textures
                                                ? material_template->parameters.size() * kUniformVectorAlignment
                                                : 0;
        size_t constant_size = bindless_index_bytes;
        for (uint32_t parameter_id = 0; parameter_id < material_template->parameters.size(); ++parameter_id)
        {
            const MaterialParameterValue *const value = materials.GetParameterValue(
                material_instance, MaterialParameterID{parameter_id});
            if (!value)
            {
                return {};
            }
            if (std::holds_alternative<MaterialTextureSamplerValue>(*value))
            {
                if (uses_bindless_textures)
                {
                    if (textures->bindless_slots.find(parameter_id) == textures->bindless_slots.end())
                    {
                        return {};
                    }
                    continue;
                }
                const std::optional<uint32_t> binding_index =
                    material_template->parameters[parameter_id].resource_binding;
                const auto texture_it = textures->textures.find(parameter_id);
                if (!binding_index || *binding_index == kMaterialConstantsBinding ||
                    HasBinding(bindings, *binding_index) || texture_it == textures->textures.end())
                {
                    return {};
                }
                bindings.push_back(graphics::SampledTextureBinding{
                    0, *binding_index, texture_it->second.texture, texture_it->second.sampler});
                continue;
            }

            constant_size = AlignUp(constant_size, GetMaterialConstantAlignment(*value));
            constant_size += GetMaterialConstantSize(*value);
        }

        UniformAllocation constants;
        if (constant_size != 0)
        {
            constants = AllocateUniform(AlignUp(constant_size, kUniformVectorAlignment));
            if (!constants.IsValid())
            {
                return {};
            }

            if (uses_bindless_textures)
            {
                for (const auto &[parameter_id, slot] : textures->bindless_slots)
                {
                    std::memcpy(static_cast<uint8_t *>(constants.mapped) +
                                    parameter_id * kUniformVectorAlignment,
                                &slot.id, sizeof(slot.id));
                }
            }
            size_t constant_offset = bindless_index_bytes;
            for (uint32_t parameter_id = 0; parameter_id < material_template->parameters.size(); ++parameter_id)
            {
                const MaterialParameterValue *const value = materials.GetParameterValue(
                    material_instance, MaterialParameterID{parameter_id});
                if (!value || std::holds_alternative<MaterialTextureSamplerValue>(*value))
                {
                    continue;
                }
                constant_offset = AlignUp(constant_offset, GetMaterialConstantAlignment(*value));
                if (const auto *const scalar = std::get_if<float>(value))
                {
                    std::memcpy(static_cast<uint8_t *>(constants.mapped) + constant_offset,
                                scalar, sizeof(*scalar));
                }
                else if (const auto *const vector = std::get_if<Vector4f>(value))
                {
                    std::memcpy(static_cast<uint8_t *>(constants.mapped) + constant_offset,
                                vector, sizeof(*vector));
                }
                constant_offset += GetMaterialConstantSize(*value);
            }
            bindings.push_back(graphics::UniformBufferBinding{
                0, kMaterialConstantsBinding, constants.buffer, constants.offset, constants.range});
        }

        graphics::ResourceBindingSetDesc descriptor_desc{};
        descriptor_desc.set = 0;
        descriptor_desc.bindings = std::move(bindings);
        const graphics::DescriptorSetHandle descriptor_set =
            AllocateResourceBindingSet(pipeline, descriptor_desc);
        if (!descriptor_set.IsValid())
        {
            return {};
        }
        return {pipeline, descriptor_set, constants, uses_bindless_textures,
                frame_index_, globals_.frame_number};
    }

    bool FrameContext::IsMaterialBindingCurrent(const FrameMaterialBinding &binding) const
    {
        return active_ && binding.IsValid() && binding.frame_index == frame_index_ &&
               binding.frame_number == globals_.frame_number;
    }

    void FrameContext::Cleanup()
    {
        ReleaseTransientBindings();
        if (backend_ && uniform_buffer_.IsValid())
        {
            backend_->DestroyBufferResource(uniform_buffer_);
        }
        backend_ = nullptr;
        uniform_buffer_ = {};
        uniform_mapped_ = nullptr;
        uniform_capacity_ = 0;
        uniform_alignment_ = 1;
        uniform_cursor_ = 0;
        render_extent_ = {};
        active_ = false;
    }

    void FrameContext::ReleaseTransientBindings()
    {
        if (backend_)
        {
            for (graphics::DescriptorSetHandle handle : transient_binding_sets_)
            {
                backend_->DestroyResourceBindingSet(handle);
            }
        }
        transient_binding_sets_.clear();
    }
}
