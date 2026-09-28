#ifndef KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_BINDINGS_H
#define KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_BINDINGS_H

#include <span>
#include <string>
#include <variant>
#include <vector>

#include "graphics/backend/common/api.h"
#include "graphics/backend/common/command_recorder.h"
#include "../render_pass_declaration.h"
#include "../render_target.h"

namespace kpengine::render
{
    struct RenderGraphPhysicalResource
    {
        RenderFrameGraphResourceHandle logical;
        RenderFrameResourceRole role = RenderFrameResourceRole::SceneColor;
        std::variant<RenderTarget *, std::vector<graphics::BufferHandle>,
                     std::vector<graphics::AccelerationStructureHandle>> physical;
    };

    class RenderGraphBindings
    {
    public:
        bool AddTexture(RenderFrameGraphResourceHandle logical, RenderFrameResourceRole role,
                        RenderTarget *target, std::string &error);
        bool AddBuffers(RenderFrameGraphResourceHandle logical, RenderFrameResourceRole role,
                        std::span<const graphics::BufferHandle> buffers, std::string &error);
        bool AddAccelerationStructures(
            RenderFrameGraphResourceHandle logical, RenderFrameResourceRole role,
            std::span<const graphics::AccelerationStructureHandle> structures,
            std::string &error);
        bool Validate(const CompiledRenderFramePlan &plan, std::string &error) const;
        bool ApplyPassTransitions(const CompiledRenderGraph &plan,
                                  const CompiledRenderGraph::Pass &pass,
                                  graphics::CommandRecorder &recorder,
                                  std::string &error) const;
        RenderTarget *ResolveWriteAttachment(const CompiledRenderGraph::Pass &pass) const;

        RenderTarget *ResolveTexture(GraphTextureHandle handle) const noexcept;
        std::span<const graphics::BufferHandle> ResolveBuffers(GraphBufferHandle handle) const noexcept;
        std::span<const graphics::AccelerationStructureHandle> ResolveAccelerationStructures(
            GraphAccelerationStructureHandle handle) const noexcept;
        std::vector<RenderGraphPhysicalResource> &Resources() noexcept { return resources_; }
        const std::vector<RenderGraphPhysicalResource> &Resources() const noexcept { return resources_; }
        void Clear() noexcept { resources_.clear(); }

    private:
        bool Add(RenderGraphPhysicalResource resource, std::string &error);
        std::vector<RenderGraphPhysicalResource> resources_;
    };
}

#endif
