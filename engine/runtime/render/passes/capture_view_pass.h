#ifndef KPENGINE_RUNTIME_RENDER_PASSES_CAPTURE_VIEW_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_CAPTURE_VIEW_PASS_H

#include "graphics/backend/common/render_backend.h"
#include "render/frame_context.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/passes/shadow_frame_output.h"
#include "render/render_camera.h"
#include "render/render_capture_service.h"
#include "render/render_target.h"

namespace kpengine::render
{
    class PreparedRenderAssetCatalog;

    struct CaptureViewFrameInputs
    {
        RenderTarget &output;
        RenderTarget &gbuffer;
        RenderTarget &directional_shadow_target;
        RenderTarget &spot_shadow_target;
        RenderTarget &point_shadow_target;
        const DirectionalShadowFrame *directional_shadow = nullptr;
        const SpotShadowFrame *spot_shadow = nullptr;
        const PointShadowFrame *point_shadow = nullptr;
        bool spot_shadow_recorded = false;
        bool point_shadow_recorded = false;
        graphics::SamplerHandle linear_sampler;
        graphics::SamplerHandle directional_shadow_sampler;
        graphics::SamplerHandle spot_shadow_sampler;
        graphics::SamplerHandle point_shadow_sampler;
        graphics::TextureHandle screen_space_ao;
        bool screen_space_ao_available = false;
    };

    class CaptureViewPass final
    {
    public:
        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets);
        void Cleanup(graphics::RenderBackend &backend);
        bool Record(FrameContext &frame_context, RenderCamera &camera, CaptureView view,
                    const CaptureViewFrameInputs &inputs,
                    const FullscreenPassResources &fullscreen_resources,
                    graphics::CommandRecorder &recorder);

    private:
        graphics::PipelineHandle pipeline_;
    };
}

#endif
