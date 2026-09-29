#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_PASS_H

#include <functional>
#include <optional>
#include <vector>

#include "graphics/backend/common/render_backend.h"
#include "render/frame_context.h"
#include "render/material/material_system.h"
#include "render/passes/scene_draw_recorder.h"
#include "render/passes/shadow_frame_output.h"
#include "render/render_profile.h"
#include "render/render_target.h"
#include "render/render_world/render_world.h"

namespace kpengine::render
{
    class PreparedRenderAssetCatalog;
    class RenderResourceResolver;

    struct ShadowPassDrawCounts
    {
        uint64_t draw_calls = 0;
        uint64_t sections = 0;
    };

    // Owns shadow scheduling state, reusable shadow GPU objects, and shadow
    // command recording. Graph ordering and attachment scopes stay in the facade.
    class ShadowPass final
    {
    public:
        bool PrepareSamplers(graphics::RenderBackend &backend);
        bool PrepareDirectionalPipeline(graphics::RenderBackend &backend,
                                       const PreparedRenderAssetCatalog &assets);
        void Cleanup(graphics::RenderBackend &backend);
        void BeginFrame() noexcept;
        void InvalidateCache() noexcept
        {
            directional_valid_ = false;
            directional_stamp_ = 0;
            point_valid_ = false;
            point_stamp_ = 0;
            point_cache_hit_ = false;
        }

        void Schedule(const std::vector<Light> &lights,
                      const std::function<bool(ShadowHandle)> &is_shadow_handle_valid,
                      SceneDrawRecorder &draw_recorder,
                      RenderResourceResolver &resource_resolver,
                      MaterialSystem &materials, const RenderWorld &render_world,
                      const Vector3f &camera_position, RenderProfileSnapshot &profile);

        bool RecordDirectional(graphics::RenderBackend &backend, RenderTarget *target,
                               FrameContext &frame_context,
                               RenderResourceResolver &resource_resolver,
                               MaterialSystem &materials,
                               SceneDrawRecorder &draw_recorder,
                               ShadowPassDrawCounts &counts);
        bool RecordSpot(graphics::RenderBackend &backend, RenderTarget *target,
                        FrameContext &frame_context,
                        RenderResourceResolver &resource_resolver,
                        MaterialSystem &materials,
                        SceneDrawRecorder &draw_recorder,
                        ShadowPassDrawCounts &counts);
        bool RecordPoint(graphics::RenderBackend &backend, RenderTarget *target,
                         FrameContext &frame_context,
                         RenderResourceResolver &resource_resolver,
                         SceneDrawRecorder &draw_recorder,
                         ShadowPassDrawCounts &counts);

        const std::optional<DirectionalShadowFrame> &DirectionalFrame() const noexcept
        {
            return directional_frame_;
        }
        const std::optional<SpotShadowFrame> &SpotFrame() const noexcept { return spot_frame_; }
        const std::optional<PointShadowFrame> &PointFrame() const noexcept { return point_frame_; }
        graphics::SamplerHandle DirectionalSampler() const noexcept { return directional_sampler_; }
        graphics::SamplerHandle SpotSampler() const noexcept { return spot_sampler_; }
        graphics::SamplerHandle PointSampler() const noexcept { return point_sampler_; }
        bool DirectionalCacheHit() const noexcept { return directional_cache_hit_; }
        bool PointCacheHit() const noexcept { return point_cache_hit_; }
        bool SpotRecorded() const noexcept { return spot_recorded_; }
        bool PointRecorded() const noexcept { return point_recorded_; }
        void MarkPointCacheReused() noexcept { point_recorded_ = true; }

    private:
        bool EnsureShadowPipeline(graphics::RenderBackend &backend,
                                  const PreparedRenderAssetCatalog &assets);
        bool EnsureSamplers(graphics::RenderBackend &backend);

        std::optional<DirectionalShadowFrame> directional_frame_;
        std::optional<SpotShadowFrame> spot_frame_;
        std::optional<PointShadowFrame> point_frame_;
        graphics::PipelineHandle pipeline_;
        graphics::SamplerHandle directional_sampler_;
        graphics::SamplerHandle spot_sampler_;
        graphics::SamplerHandle point_sampler_;
        bool spot_recorded_ = false;
        bool point_recorded_ = false;
        bool directional_cache_hit_ = false;
        bool directional_valid_ = false;
        uint64_t directional_stamp_ = 0;
        bool point_cache_hit_ = false;
        bool point_valid_ = false;
        uint64_t point_stamp_ = 0;
    };
}

#endif
