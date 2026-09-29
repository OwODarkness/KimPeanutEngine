#ifndef KPENGINE_RUNTIME_RENDER_PASSES_FULLSCREEN_PASS_RESOURCES_H
#define KPENGINE_RUNTIME_RENDER_PASSES_FULLSCREEN_PASS_RESOURCES_H

#include "graphics/backend/common/render_backend.h"

namespace kpengine::render
{
    // Render-owned shared mesh and sampler borrowed by fullscreen pass owners.
    class FullscreenPassResources final
    {
    public:
        bool Initialize(graphics::RenderBackend &backend);
        void Cleanup(graphics::RenderBackend &backend);

        graphics::MeshHandle Mesh() const noexcept { return mesh_; }
        graphics::SamplerHandle LinearSampler() const noexcept { return linear_sampler_; }
        bool IsReady() const noexcept { return mesh_.IsValid() && linear_sampler_.IsValid(); }

    private:
        graphics::MeshHandle mesh_{};
        graphics::SamplerHandle linear_sampler_{};
    };
}

#endif
