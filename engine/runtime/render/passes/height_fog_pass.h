#ifndef KPENGINE_RUNTIME_RENDER_HEIGHT_FOG_PASS_H
#define KPENGINE_RUNTIME_RENDER_HEIGHT_FOG_PASS_H

#include "graphics/backend/common/api.h"
#include "render/height_fog_settings.h"
#include "math/math_header.h"

namespace kpengine::graphics
{
    class CommandRecorder;
    class RenderBackend;
}

namespace kpengine::render
{
    class FrameContext;
    class FullscreenPassResources;
    class PreparedRenderAssetCatalog;
    class RenderCamera;
    class RenderTarget;

    class HeightFogPass final
    {
    public:
        HeightFogPass() = default;
        HeightFogPass(const HeightFogPass &) = delete;
        HeightFogPass &operator=(const HeightFogPass &) = delete;

        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets);
        void Cleanup(graphics::RenderBackend &backend);
        bool Record(FrameContext &frame, const RenderCamera &camera, Vector2f jitter,
                    const HeightFogSettings &settings,
                    const RenderTarget &gbuffer, const FullscreenPassResources &fullscreen,
                    graphics::CommandRecorder &recorder) const;

    private:
        graphics::PipelineHandle pipeline_{};
    };
}

#endif
