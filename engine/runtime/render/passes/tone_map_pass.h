#ifndef KPENGINE_RUNTIME_RENDER_PASSES_TONE_MAP_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_TONE_MAP_PASS_H

#include <cstdint>

#include "graphics/backend/common/render_backend.h"
#include "render/frame_context.h"
#include "render/path_trace_settings.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/render_target.h"

namespace kpengine::render
{
    class PreparedRenderAssetCatalog;

    struct ToneMapOutputPolicy
    {
        float exposure = 0.0f;
        uint32_t tone_map_operator = 0;
        uint32_t output_transfer = 0;
    };

    class ToneMapPass final
    {
    public:
        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets);
        void Cleanup(graphics::RenderBackend &backend);
        uint64_t ShaderSignature() const noexcept { return shader_signature_; }
        graphics::PipelineHandle Pipeline() const noexcept { return pipeline_; }
        ToneMapOutputPolicy OutputPolicy() const noexcept;

        bool Record(FrameContext &frame_context, RenderTarget &hdr_source,
                    RenderTarget *gbuffer_source, RenderTarget *path_trace_guide,
                    RenderTarget &scene_output,
                    const FullscreenPassResources &fullscreen_resources,
                    graphics::CommandRecorder &recorder,
                    bool path_tracing_active, const PathTraceSettings &settings,
                    uint32_t sample_count);

    private:
        graphics::PipelineHandle pipeline_;
        uint64_t shader_signature_ = 0;
    };
}

#endif
