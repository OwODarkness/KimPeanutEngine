#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SCREEN_SPACE_AO_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SCREEN_SPACE_AO_PASS_H

#include <cstdint>

#include "graphics/backend/common/render_backend.h"
#include "render/render_target.h"

namespace kpengine::render
{
    class FrameContext;
    class PreparedRenderAssetCatalog;
    class RenderCamera;
    class FullscreenPassResources;

    enum class ScreenSpaceAoQuality : std::uint8_t
    {
        Low,
        Medium,
        High,
    };

    constexpr std::uint32_t ScreenSpaceAoSampleCount(ScreenSpaceAoQuality quality) noexcept
    {
        switch (quality)
        {
        case ScreenSpaceAoQuality::Low: return 6u;
        case ScreenSpaceAoQuality::Medium: return 12u;
        case ScreenSpaceAoQuality::High: return 24u;
        }
        return 12u;
    }

    struct ScreenSpaceAoSettings
    {
        bool enabled = true;
        float radius = 0.65f;
        float bias = 0.025f;
        float strength = 1.0f;
        ScreenSpaceAoQuality quality = ScreenSpaceAoQuality::Medium;
    };

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
