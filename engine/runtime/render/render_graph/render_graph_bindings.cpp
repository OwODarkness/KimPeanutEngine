#include "render_graph_bindings.h"

#include <algorithm>
#include <type_traits>

namespace kpengine::render
{
    namespace
    {
        graphics::ResourceUsage ToResourceUsage(RenderGraphUsage usage)
        {
            switch (usage)
            {
            case RenderGraphUsage::Sampled: return graphics::ResourceUsage::Sampled;
            case RenderGraphUsage::ColorAttachment: return graphics::ResourceUsage::ColorAttachment;
            case RenderGraphUsage::DepthAttachment: return graphics::ResourceUsage::DepthAttachment;
            case RenderGraphUsage::TransferSource: return graphics::ResourceUsage::TransferSource;
            case RenderGraphUsage::TransferDestination: return graphics::ResourceUsage::TransferDestination;
            case RenderGraphUsage::AccelerationStructureBuildInput:
                return graphics::ResourceUsage::AccelerationStructureBuildInput;
            case RenderGraphUsage::AccelerationStructureBuildOutput:
                return graphics::ResourceUsage::AccelerationStructureBuildOutput;
            case RenderGraphUsage::AccelerationStructureRead:
                return graphics::ResourceUsage::AccelerationStructureRead;
            case RenderGraphUsage::StorageRead: return graphics::ResourceUsage::StorageRead;
            case RenderGraphUsage::StorageWrite: return graphics::ResourceUsage::StorageWrite;
            case RenderGraphUsage::Present: return graphics::ResourceUsage::Present;
            case RenderGraphUsage::Undefined: break;
            }
            return graphics::ResourceUsage::Undefined;
        }

        graphics::RenderTargetAttachmentScope ToAttachmentScope(
            RenderGraphAttachmentScope scope)
        {
            return scope.all ? graphics::RenderTargetAttachmentScope{}
                             : graphics::RenderTargetAttachmentScope{
                                   false, scope.color_mask, scope.depth};
        }

        template <typename T>
        bool Contains(std::span<const T> values, const T &value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        bool IsNonEmpty(const RenderGraphPhysicalResource &resource)
        {
            return std::visit(
                [](const auto &physical) {
                    using T = std::decay_t<decltype(physical)>;
                    if constexpr (std::is_same_v<T, RenderTarget *>)
                    {
                        return physical != nullptr;
                    }
                    else
                    {
                        return !physical.empty() &&
                               std::all_of(physical.begin(), physical.end(),
                                           [](const auto handle) { return handle.IsValid(); });
                    }
                },
                resource.physical);
        }
    }

    bool RenderGraphBindings::Add(RenderGraphPhysicalResource resource, std::string &error)
    {
        const auto existing = std::find_if(resources_.begin(), resources_.end(),
                                            [&resource](const auto &entry) {
                                                return entry.logical == resource.logical;
                                            });
        if (existing != resources_.end())
        {
            error = "A graph handle was bound more than once";
            return false;
        }
        if (!IsNonEmpty(resource))
        {
            error = "A required graph binding is empty or contains an invalid handle";
            return false;
        }
        resources_.push_back(std::move(resource));
        return true;
    }

    bool RenderGraphBindings::AddTexture(RenderFrameGraphResourceHandle logical,
                                         RenderFrameResourceRole role, RenderTarget *target,
                                         std::string &error)
    {
        if (!std::holds_alternative<GraphTextureHandle>(logical))
        {
            error = "Texture binding received a non-texture graph handle";
            return false;
        }
        return Add({std::move(logical), role, target}, error);
    }

    bool RenderGraphBindings::AddBuffers(RenderFrameGraphResourceHandle logical,
                                         RenderFrameResourceRole role,
                                         std::span<const graphics::BufferHandle> buffers,
                                         std::string &error)
    {
        if (!std::holds_alternative<GraphBufferHandle>(logical))
        {
            error = "Buffer binding received a non-buffer graph handle";
            return false;
        }
        std::vector<graphics::BufferHandle> unique;
        unique.reserve(buffers.size());
        for (const graphics::BufferHandle buffer : buffers)
        {
            if (!Contains<graphics::BufferHandle>(unique, buffer))
            {
                unique.push_back(buffer);
            }
        }
        return Add({std::move(logical), role, std::move(unique)}, error);
    }

    bool RenderGraphBindings::AddAccelerationStructures(
        RenderFrameGraphResourceHandle logical, RenderFrameResourceRole role,
        std::span<const graphics::AccelerationStructureHandle> structures, std::string &error)
    {
        if (!std::holds_alternative<GraphAccelerationStructureHandle>(logical))
        {
            error = "Acceleration structure binding received a different graph handle kind";
            return false;
        }
        std::vector<graphics::AccelerationStructureHandle> unique;
        unique.reserve(structures.size());
        for (const graphics::AccelerationStructureHandle structure : structures)
        {
            if (!Contains<graphics::AccelerationStructureHandle>(unique, structure))
            {
                unique.push_back(structure);
            }
        }
        return Add({std::move(logical), role, std::move(unique)}, error);
    }

    bool RenderGraphBindings::Validate(const CompiledRenderFramePlan &plan,
                                       std::string &error) const
    {
        for (const RenderFrameResourceImport &import : plan.resources)
        {
            const auto logical_matches = std::count_if(
                resources_.begin(), resources_.end(), [&import](const auto &binding) {
                    return binding.logical == import.handle && binding.role == import.role;
                });
            if (logical_matches != 1)
            {
                error = "A compiled graph resource is missing a unique role-matched physical binding";
                return false;
            }
        }
        if (resources_.size() != plan.resources.size())
        {
            error = "Physical bindings contain an undeclared or foreign graph resource";
            return false;
        }
        for (const RenderGraphPhysicalResource &resource : resources_)
        {
            const auto import = std::find_if(
                plan.resources.begin(), plan.resources.end(), [&resource](const auto &entry) {
                    return entry.handle == resource.logical && entry.role == resource.role;
                });
            if (import == plan.resources.end() || !IsNonEmpty(resource))
            {
                error = "Physical binding kind, identity, role, or required group is invalid";
                return false;
            }
        }
        return true;
    }

    bool RenderGraphBindings::ApplyPassTransitions(
        const CompiledRenderGraph &plan, const CompiledRenderGraph::Pass &pass,
        graphics::CommandRecorder &recorder, std::string &error) const
    {
        if (pass.transition_offset + pass.transition_count > plan.Transitions().size())
        {
            error = "Pass transition range falls outside the compiled plan";
            return false;
        }
        for (const RenderGraphResourceUse &use : pass.uses)
        {
            if (const auto *texture = std::get_if<GraphTextureHandle>(&use.handle))
            {
                if (ResolveTexture(*texture) == nullptr)
                {
                    error = "Pass uses a texture without a physical binding";
                    return false;
                }
            }
            else if (const auto *buffer = std::get_if<GraphBufferHandle>(&use.handle))
            {
                if (ResolveBuffers(*buffer).empty())
                {
                    error = "Pass uses a buffer group without physical members";
                    return false;
                }
            }
            else if (ResolveAccelerationStructures(
                         std::get<GraphAccelerationStructureHandle>(use.handle)).empty())
            {
                error = "Pass uses an acceleration structure without physical members";
                return false;
            }
        }

        const auto &transitions = plan.Transitions();
        for (std::size_t index = 0; index < pass.transition_count; ++index)
        {
            const RenderGraphTransitionIntent &intent =
                transitions[pass.transition_offset + index];
            const graphics::ResourceUsage usage = ToResourceUsage(intent.usage);
            if (const auto *texture = std::get_if<GraphTextureHandle>(&intent.handle))
            {
                RenderTarget *target = ResolveTexture(*texture);
                if (target == nullptr || !recorder.RequireRenderTargetUsage(
                                             target->GetHandle(), usage,
                                             ToAttachmentScope(intent.scope)))
                {
                    error = "Render target usage requirement failed";
                    return false;
                }
            }
            else if (const auto *buffer = std::get_if<GraphBufferHandle>(&intent.handle))
            {
                for (const graphics::BufferHandle physical : ResolveBuffers(*buffer))
                {
                    if (!recorder.RequireBufferUsage(physical, usage))
                    {
                        error = "Buffer group usage requirement failed";
                        return false;
                    }
                }
            }
            else
            {
                const auto handle = std::get<GraphAccelerationStructureHandle>(intent.handle);
                for (const graphics::AccelerationStructureHandle physical :
                     ResolveAccelerationStructures(handle))
                {
                    if (!recorder.RequireAccelerationStructureUsage(physical, usage))
                    {
                        error = "Acceleration structure group usage requirement failed";
                        return false;
                    }
                }
            }
        }
        return true;
    }

    RenderTarget *RenderGraphBindings::ResolveWriteAttachment(
        const CompiledRenderGraph::Pass &pass) const
    {
        for (const RenderGraphResourceUse &use : pass.uses)
        {
            if (use.access == RenderGraphAccess::Write &&
                (use.usage == RenderGraphUsage::ColorAttachment ||
                 use.usage == RenderGraphUsage::DepthAttachment))
            {
                if (const auto *texture = std::get_if<GraphTextureHandle>(&use.handle))
                {
                    return ResolveTexture(*texture);
                }
            }
        }
        return nullptr;
    }

    RenderTarget *RenderGraphBindings::ResolveTexture(GraphTextureHandle handle) const noexcept
    {
        const RenderFrameGraphResourceHandle logical = handle;
        const auto it = std::find_if(resources_.begin(), resources_.end(),
                                     [&logical](const auto &resource) {
                                         return resource.logical == logical;
                                     });
        if (it == resources_.end())
        {
            return nullptr;
        }
        const auto *target = std::get_if<RenderTarget *>(&it->physical);
        return target != nullptr ? *target : nullptr;
    }

    std::span<const graphics::BufferHandle> RenderGraphBindings::ResolveBuffers(
        GraphBufferHandle handle) const noexcept
    {
        const RenderFrameGraphResourceHandle logical = handle;
        const auto it = std::find_if(resources_.begin(), resources_.end(),
                                     [&logical](const auto &resource) {
                                         return resource.logical == logical;
                                     });
        if (it == resources_.end())
        {
            return {};
        }
        const auto *buffers = std::get_if<std::vector<graphics::BufferHandle>>(&it->physical);
        return buffers != nullptr ? std::span<const graphics::BufferHandle>(*buffers)
                                  : std::span<const graphics::BufferHandle>{};
    }

    std::span<const graphics::AccelerationStructureHandle>
    RenderGraphBindings::ResolveAccelerationStructures(
        GraphAccelerationStructureHandle handle) const noexcept
    {
        const RenderFrameGraphResourceHandle logical = handle;
        const auto it = std::find_if(resources_.begin(), resources_.end(),
                                     [&logical](const auto &resource) {
                                         return resource.logical == logical;
                                     });
        if (it == resources_.end())
        {
            return {};
        }
        const auto *structures = std::get_if<
            std::vector<graphics::AccelerationStructureHandle>>(&it->physical);
        return structures != nullptr
                   ? std::span<const graphics::AccelerationStructureHandle>(*structures)
                   : std::span<const graphics::AccelerationStructureHandle>{};
    }
}
