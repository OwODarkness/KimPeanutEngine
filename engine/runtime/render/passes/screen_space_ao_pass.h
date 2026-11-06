#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SCREEN_SPACE_AO_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SCREEN_SPACE_AO_PASS_H

#include <cstdint>

#include "graphics/backend/common/render_backend.h"
#include "render/render_target.h"
#include "render/screen_space_ao_settings.h"

namespace kpengine::render
{
    class FrameContext;
    class PreparedRenderAssetCatalog;
    class RenderCamera;
    class FullscreenPassResources;

    class ScreenSpaceAoPass final
    {
    public:
        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets);
        void SetSettings(ScreenSpaceAoSettings settings) noexcept { settings_ = settings; }
        void Cleanup(graphics::RenderBackend &backend);
        bool RecordEstimate(FrameContext &frame_context, RenderCamera &camera,
                            RenderTarget &gbuffer, const FullscreenPassResources &fullscreen,
                            graphics::CommandRecorder &recorder);
        bool RecordFilter(FrameContext &frame_context, RenderTarget &gbuffer,
                          RenderTarget &raw_ao, const FullscreenPassResources &fullscreen,
                          graphics::CommandRecorder &recorder);

    private:
        graphics::PipelineHandle estimate_pipeline_{};
        graphics::PipelineHandle filter_pipeline_{};
        ScreenSpaceAoSettings settings_{};
    };
}

#endif
