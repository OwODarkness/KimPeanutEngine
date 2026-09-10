#include "frame_context.h"

#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <limits>

#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
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

        uint64_t HashCombine(uint64_t hash, uint64_t value)
        {
            constexpr uint64_t kFnvPrime = 1099511628211ull;
            hash ^= value;
            return hash * kFnvPrime;
        }

        uint64_t HashHandle(uint64_t hash, uint32_t id, uint32_t generation)
        {
            return HashCombine(HashCombine(hash, id), generation);
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
        uniform_cursor_ = stable_uniform_cursor_;
        profile_counters_ = {};
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
        const UniformAllocation allocation{
            uniform_buffer_, offset, size, static_cast<uint8_t *>(uniform_mapped_) + offset};
        backend_->MarkUniformBufferRangeWritten(allocation.buffer, allocation.offset,
                                                 allocation.range);
        return allocation;
    }

    UniformAllocation FrameContext::UpdateStableUniform(uint64_t key, const void *data, size_t size)
    {
        if (!active_ || !data || size == 0)
        {
            return {};
        }

        StableUniformRecord &record = stable_uniforms_[key];
        if (!record.allocation.IsValid() || record.allocation.range != size)
        {
            record.allocation = AllocateUniform(size);
            if (!record.allocation.IsValid())
            {
                return {};
            }
            stable_uniform_cursor_ = std::max(
                stable_uniform_cursor_, record.allocation.offset + record.allocation.range);
            record.initialized = false;
        }

        if (!record.initialized || std::memcmp(record.allocation.mapped, data, size) != 0)
        {
            const auto started = std::chrono::steady_clock::now();
            std::memcpy(record.allocation.mapped, data, size);
            backend_->MarkUniformBufferRangeWritten(record.allocation.buffer,
                                                     record.allocation.offset,
                                                     record.allocation.range);
            RecordUniformWrite(
                size,
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            record.initialized = true;
        }
        return record.allocation;
    }

    FrameResourceBinding FrameContext::CreateOrGetStableBindingSet(
        uint64_t key, graphics::PipelineHandle pipeline,
        const std::vector<graphics::ResourceBinding> &bindings)
    {
        if (!active_ || !backend_ || !pipeline.IsValid())
        {
            return {};
        }

        const uint64_t binding_cache_key = HashHandle(key, pipeline.id, pipeline.generation);
        auto cached = stable_binding_sets_.find(binding_cache_key);
        if (cached != stable_binding_sets_.end())
        {
            FrameResourceBinding result{cached->second.descriptor_set, {}};
            for (const uint32_t binding_index : cached->second.dynamic_bindings)
            {
                const auto binding = std::find_if(
                    bindings.begin(), bindings.end(),
                    [binding_index](const graphics::ResourceBinding &candidate)
                    {
                        return std::visit(
                            [binding_index](const auto &value)
                            {
                                return value.binding == binding_index;
                            },
                            candidate);
                    });
                if (binding == bindings.end())
                {
                    return {};
                }
                const auto *uniform = std::get_if<graphics::UniformBufferBinding>(&*binding);
                if (!uniform || uniform->offset > std::numeric_limits<uint32_t>::max())
                {
                    return {};
                }
                result.dynamic_offsets.push_back(static_cast<uint32_t>(uniform->offset));
            }
            return result;
        }

        std::vector<graphics::ResourceBinding> descriptor_bindings = bindings;
        std::sort(descriptor_bindings.begin(), descriptor_bindings.end(),
                  [](const graphics::ResourceBinding &left, const graphics::ResourceBinding &right)
                  {
                      const uint32_t left_binding = std::visit(
                          [](const auto &value) { return value.binding; }, left);
                      const uint32_t right_binding = std::visit(
                          [](const auto &value) { return value.binding; }, right);
                      return left_binding < right_binding;
                  });

        StableBindingRecord record{};
        for (graphics::ResourceBinding &binding : descriptor_bindings)
        {
            if (std::holds_alternative<graphics::UniformBufferBinding>(binding))
            {
                const auto &uniform = std::get<graphics::UniformBufferBinding>(binding);
                if (uniform.offset > std::numeric_limits<uint32_t>::max())
                {
                    return {};
                }
                record.dynamic_bindings.push_back(uniform.binding);
                auto &stable_uniform = std::get<graphics::UniformBufferBinding>(binding);
                stable_uniform.offset = 0;
            }
        }

        graphics::ResourceBindingSetDesc descriptor_desc{};
        if (!descriptor_bindings.empty())
        {
            descriptor_desc.set = std::visit(
                [](const auto &value) { return value.set; }, descriptor_bindings.front());
            for (const graphics::ResourceBinding &binding : descriptor_bindings)
            {
                const uint32_t set = std::visit(
                    [](const auto &value) { return value.set; }, binding);
                if (set != descriptor_desc.set)
                {
                    return {};
                }
            }
        }
        descriptor_desc.bindings = std::move(descriptor_bindings);
        descriptor_desc.persistent = true;
        record.descriptor_set = backend_->CreateResourceBindingSet(pipeline, descriptor_desc);
        if (!record.descriptor_set.IsValid())
        {
            return {};
        }
        stable_binding_sets_.emplace(binding_cache_key, record);

        FrameResourceBinding result{record.descriptor_set, {}};
        for (const uint32_t binding_index : record.dynamic_bindings)
        {
            const auto binding = std::find_if(
                bindings.begin(), bindings.end(),
                [binding_index](const graphics::ResourceBinding &candidate)
                {
                    return std::visit(
                        [binding_index](const auto &value)
                        {
                            return value.binding == binding_index;
                        },
                        candidate);
                });
            if (binding == bindings.end())
            {
                return {};
            }
            result.dynamic_offsets.push_back(static_cast<uint32_t>(
                std::get<graphics::UniformBufferBinding>(*binding).offset));
        }
        return result;
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

    bool FrameContext::WriteFrameBuffer(const graphics::BufferHandle buffer,
                                        const std::size_t offset, const void *data,
                                        const std::size_t size)
    {
        if (!active_ || !backend_ || !buffer.IsValid() || !data || size == 0u)
        {
            return false;
        }
        return backend_->WriteFrameBuffer(buffer, offset, data, size);
    }

    FrameLightingBinding FrameContext::CreateLightingBinding(const LightGpuFrameData &lighting_data)
    {
        if (!active_ || !IsLightGpuFrameHeaderCompatible(lighting_data.header))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Failed to allocate frame lighting binding: active=%s header_valid=%s used=%zu capacity=%zu",
                   active_ ? "yes" : "no",
                   IsLightGpuFrameHeaderCompatible(lighting_data.header) ? "yes" : "no",
                   uniform_cursor_, uniform_capacity_);
            return {};
        }
        const UniformAllocation allocation = AllocateUniform(lighting_data);
        if (!allocation.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Failed to allocate frame lighting constants: used=%zu requested=%zu capacity=%zu",
                   uniform_cursor_, sizeof(lighting_data), uniform_capacity_);
            return {};
        }
        return {allocation, frame_index_, globals_.frame_number};
    }

    FrameMaterialBinding FrameContext::CreateMaterialBinding(
        const MaterialSystem &materials, const RenderResourceResolver &resolver,
        MaterialInstanceHandle material_instance,
        const std::vector<graphics::ResourceBinding> &draw_bindings, MaterialPass pass)
    {
        const auto material_resolution_started = std::chrono::steady_clock::now();
        bool material_resolution_timer_stopped = false;
        const auto stop_material_resolution_timer = [&]()
        {
            if (!material_resolution_timer_stopped)
            {
                profile_counters_.material_resolution_cpu_ms +=
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - material_resolution_started)
                        .count();
                material_resolution_timer_stopped = true;
            }
        };
        if (!active_ ||
            materials.GetInstanceResolution(material_instance).state != MaterialResourceState::Ready)
        {
            return {};
        }

        const MaterialTemplateHandle template_handle = materials.GetInstanceTemplate(material_instance);
        const MaterialTemplateDesc *const material_template = materials.FindTemplate(template_handle);
        const graphics::PipelineHandle pipeline = resolver.FindMaterialPipeline(template_handle, pass);
        const auto *const textures = resolver.FindTextureBindings(material_instance);
        if (!material_template || !pipeline.IsValid() || !textures)
        {
            return {};
        }

        if (HasBinding(draw_bindings, kMaterialConstantsBinding))
        {
            return {};
        }

        uint64_t material_key = 1469598103934665603ull;
        material_key = HashHandle(material_key, pipeline.id, pipeline.generation);
        material_key = HashHandle(material_key, material_instance.id, material_instance.generation);
        material_key = HashCombine(material_key, static_cast<uint64_t>(pass));
        material_key = HashCombine(material_key, materials.GetInstanceRevision(material_instance));
        material_key = HashCombine(material_key, textures->uses_bindless_textures ? 1ull : 0ull);
        for (uint32_t parameter_id = 0; parameter_id < material_template->parameters.size(); ++parameter_id)
        {
            const auto texture_it = textures->textures.find(parameter_id);
            if (texture_it != textures->textures.end())
            {
                material_key = HashHandle(material_key, texture_it->second.texture.id,
                                          texture_it->second.texture.generation);
                material_key = HashHandle(material_key, texture_it->second.sampler.id,
                                          texture_it->second.sampler.generation);
            }
            const auto bindless_it = textures->bindless_slots.find(parameter_id);
            if (bindless_it != textures->bindless_slots.end())
            {
                material_key = HashHandle(material_key, bindless_it->second.id,
                                          bindless_it->second.generation);
            }
        }

        CachedMaterialRecord *cached_material = nullptr;
        const auto cached_material_it = cached_materials_.find(material_key);
        if (cached_material_it != cached_materials_.end())
        {
            cached_material = &cached_material_it->second;
        }

        if (!cached_material)
        {
            CachedMaterialRecord candidate{};
            std::vector<graphics::ResourceBinding> material_bindings;
            const bool uses_bindless_textures = textures->uses_bindless_textures;

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
                    stop_material_resolution_timer();
                    return {};
                }
                if (std::holds_alternative<MaterialTextureSamplerValue>(*value))
                {
                    if (uses_bindless_textures)
                    {
                        if (textures->bindless_slots.find(parameter_id) == textures->bindless_slots.end())
                        {
                            stop_material_resolution_timer();
                            return {};
                        }
                        continue;
                    }
                    const std::optional<uint32_t> binding_index =
                        material_template->parameters[parameter_id].resource_binding;
                    const auto texture_it = textures->textures.find(parameter_id);
                    if (!binding_index || *binding_index == kMaterialConstantsBinding ||
                        HasBinding(draw_bindings, *binding_index) || texture_it == textures->textures.end())
                    {
                        stop_material_resolution_timer();
                        return {};
                    }
                    material_bindings.push_back(graphics::SampledTextureBinding{
                        0, *binding_index, texture_it->second.texture, texture_it->second.sampler});
                    continue;
                }

                constant_size = AlignUp(constant_size, GetMaterialConstantAlignment(*value));
                constant_size += GetMaterialConstantSize(*value);
            }

            stop_material_resolution_timer();
            if (constant_size != 0)
            {
                std::vector<uint8_t> constant_data(AlignUp(constant_size, kUniformVectorAlignment), 0);
                if (uses_bindless_textures)
                {
                    for (const auto &[parameter_id, slot] : textures->bindless_slots)
                    {
                        std::memcpy(constant_data.data() + parameter_id * kUniformVectorAlignment,
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
                        std::memcpy(constant_data.data() + constant_offset, scalar, sizeof(*scalar));
                    }
                    else if (const auto *const vector = std::get_if<Vector4f>(value))
                    {
                        std::memcpy(constant_data.data() + constant_offset, vector, sizeof(*vector));
                    }
                    constant_offset += GetMaterialConstantSize(*value);
                }

                candidate.constants = UpdateStableUniform(
                    HashCombine(material_key, 0x4d4154434f4e5354ull),
                    constant_data.data(), constant_data.size());
                if (!candidate.constants.IsValid())
                {
                    return {};
                }
                material_bindings.push_back(graphics::UniformBufferBinding{
                    0, kMaterialConstantsBinding, candidate.constants.buffer,
                    candidate.constants.offset, candidate.constants.range});
            }

            candidate.bindings = std::move(material_bindings);
            candidate.uses_bindless_textures = uses_bindless_textures;
            cached_material = &cached_materials_.emplace(material_key, std::move(candidate)).first->second;
        }

        std::vector<graphics::ResourceBinding> bindings = draw_bindings;
        bindings.insert(bindings.end(), cached_material->bindings.begin(),
                        cached_material->bindings.end());
        const FrameResourceBinding resource_binding =
            CreateOrGetStableBindingSet(material_key, pipeline, bindings);
        if (!resource_binding.IsValid())
        {
            return {};
        }
        return {pipeline, resource_binding.descriptor_set, cached_material->constants,
                resource_binding.dynamic_offsets, cached_material->uses_bindless_textures,
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
        ReleaseStableBindings();
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
        stable_uniform_cursor_ = 0;
        stable_uniforms_.clear();
        cached_materials_.clear();
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

    void FrameContext::ReleaseStableBindings()
    {
        if (backend_)
        {
            for (const auto &[key, record] : stable_binding_sets_)
            {
                (void)key;
                if (record.descriptor_set.IsValid())
                {
                    backend_->DestroyResourceBindingSet(record.descriptor_set);
                }
            }
        }
        stable_binding_sets_.clear();
    }
}
