#ifndef KPENGINE_RUNTIME_RENDER_PASSES_DEFERRED_LIGHTING_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_DEFERRED_LIGHTING_PASS_H

#include <cstdint>
#include <utility>

#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "render/environment_frame_bindings.h"
#include "render/frame_context.h"
#include "render/light/light_gpu_data.h"
#include "render/material/material_system.h"
#include "render/passes/scene_draw_recorder.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/passes/environment_bindings_owner.h"
#include "render/passes/shadow_frame_output.h"
#include "render/render_camera.h"
#include "render/render_target.h"

namespace kpengine::render
{
    class PreparedRenderAssetCatalog;

    class RenderResourceResolver;

    struct DeferredLightingRecordResult
    {
        bool succeeded = false;
        bool ray_query_shadows_active = false;
        uint64_t draw_calls = 0;
        uint64_t sections = 0;
        uint64_t triangles = 0;
    };

    struct DeferredLightingFrameInputs
    {
        const FrameLightingBinding &frame_lighting;
        const EnvironmentFrameBindings &environment;
        RenderTarget &gbuffer;
        graphics::TextureHandle screen_space_ao;
        bool screen_space_ao_enabled = false;
        RenderTarget &scene_hdr;
        RenderTarget &directional_shadow_target;
        RenderTarget &spot_shadow_target;
        RenderTarget &point_shadow_target;
        const DirectionalShadowFrame *directional_shadow = nullptr;
        const SpotShadowFrame *spot_shadow = nullptr;
        const PointShadowFrame *point_shadow = nullptr;
        bool point_shadow_recorded = false;
        graphics::SamplerHandle directional_shadow_sampler;
        graphics::SamplerHandle spot_shadow_sampler;
        graphics::SamplerHandle point_shadow_sampler;
        graphics::AccelerationStructureHandle scene_tlas;
        bool ray_query_shadows_requested = false;
    };

    class DeferredLightingPass final
    {
    public:
        ~DeferredLightingPass() = default;
        DeferredLightingPass() = default;
        DeferredLightingPass(const DeferredLightingPass &) = delete;
        DeferredLightingPass &operator=(const DeferredLightingPass &) = delete;

        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets,
                              bool ray_query_shadows_supported);
        void Cleanup(graphics::RenderBackend &backend);
        bool HasRayQueryPipeline() const noexcept { return ray_query_pipeline_.IsValid(); }
        bool ResourcesReady(bool ray_query_shadows_supported) const noexcept
        {
            return pipeline_.IsValid() &&
                   ray_query_shadows_supported_ == ray_query_shadows_supported &&
                   (!ray_query_shadows_supported || ray_query_pipeline_.IsValid());
        }

        void UpdateEnvironment(
            const std::optional<EnvironmentSourceDesc> &source,
            const std::optional<EnvironmentSourceHandle> &source_handle,
            RenderResourceResolver &resolver,
            const PreparedRenderAssetCatalog &prepared_assets);
        bool EnsureEnvironmentFallback(RenderResourceResolver &resolver);
        void ClearFrameLightingBinding() noexcept { frame_lighting_binding_ = {}; }
        void ClearEnvironment() noexcept { environment_bindings_owner_.Clear(); }
        void SetFrameLightingBinding(FrameLightingBinding binding) noexcept
        {
            frame_lighting_binding_ = std::move(binding);
        }
        const FrameLightingBinding &FrameLighting() const noexcept
        {
            return frame_lighting_binding_;
        }
        const EnvironmentFrameBindings &Environment() const noexcept
        {
            return environment_bindings_owner_.Active();
        }

        DeferredLightingRecordResult RecordGBuffer(
            FrameContext &frame_context, RenderCamera &camera,
            SceneDrawRecorder &draw_recorder, MaterialSystem &materials,
            RenderResourceResolver &resource_resolver,
            graphics::CommandRecorder &recorder, RenderTarget *target);

        DeferredLightingRecordResult RecordLighting(
            FrameContext &frame_context, RenderCamera &camera,
            const DeferredLightingFrameInputs &inputs,
            const FullscreenPassResources &fullscreen_resources,
            graphics::CommandRecorder &recorder);

    private:
        EnvironmentBindingsOwner environment_bindings_owner_;
        FrameLightingBinding frame_lighting_binding_;
        graphics::PipelineHandle pipeline_;
        graphics::PipelineHandle ray_query_pipeline_;
        bool ray_query_shadows_supported_ = false;
        bool ray_query_shadow_path_active_ = false;
    };
}

#endif
