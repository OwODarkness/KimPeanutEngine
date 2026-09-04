#ifndef KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H
#define KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "asset/common.h"
#include "graphics/backend/common/api.h"
#include "graphics/backend/common/render_backend.h"
#include "render_capture_service.h"
#include "environment_source.h"
#include "frame_context.h"
#include "render/light/light_world.h"
#include "render/material/material_system.h"
#include "render_camera.h"
#include "render_pass.h"
#include "render_resource.h"
#include "prepared_render_asset_catalog.h"
#include "render_world/render_world.h"
#include "renderer_frame_targets.h"

namespace kpengine::data
{
    struct TextureData;
}

namespace kpengine::render
{
    class RenderResourceResolver;

    struct DeferredRendererInitInfo
    {
        graphics::RenderBackend &backend;
        RenderResourceResolver &resource_resolver;
        MaterialSystem &materials;
        const PreparedRenderAssetCatalog &prepared_assets;
    };

    struct DeferredRendererInitResult
    {
        bool success = false;
        std::string diagnostic;

        explicit operator bool() const { return success; }
    };

    struct RenderSceneFrameInput;

    struct DeferredRendererFrameResult
    {
        bool normal_recording_completed = true;
        bool capture_target_ready = false;
    };

    // Concrete owner of deferred render policy, pass-private state, and the
    // logical targets used by one RenderSystem frame bracket.
    class DeferredRenderer final
    {
    public:
        DeferredRenderer() = default;
        ~DeferredRenderer();
        DeferredRenderer(const DeferredRenderer &) = delete;
        DeferredRenderer &operator=(const DeferredRenderer &) = delete;

        DeferredRendererInitResult Initialize(const DeferredRendererInitInfo &info,
                                              uint32_t width, uint32_t height);
        void Cleanup();

        void RequestExtent(uint32_t width, uint32_t height);
        void ApplyPendingExtent();
        const RenderTarget &GetSceneRenderTarget() const;
        graphics::RenderTargetHandle GetCaptureTarget(CaptureView view) const;
        bool IsPassSequenceValid() const { return pass_sequence_.has_value(); }

        bool ExecuteEditorCompositePass(const std::function<void()> &record_pass);
        bool FinalizeFrame();

        DeferredRendererFrameResult RecordFrame(
            FrameContext &frame_context, const RenderSceneFrameInput &input);

    private:
        struct EnvironmentBindingBundle
        {
            asset::AssetID source_asset;
            TextureBinding panorama;
            TextureBinding irradiance;
            TextureBinding prefiltered_radiance;
            TextureBinding brdf_lut;
            uint32_t prefilter_level_count = 0;
            float ibl_intensity = 0.25f;
            bool ibl_enabled = false;

            bool HasCompleteBindings() const
            {
                return panorama.texture.IsValid() && panorama.sampler.IsValid() &&
                       irradiance.texture.IsValid() && irradiance.sampler.IsValid() &&
                       prefiltered_radiance.texture.IsValid() &&
                       prefiltered_radiance.sampler.IsValid() && brdf_lut.texture.IsValid() &&
                       brdf_lut.sampler.IsValid();
            }
        };

        struct PointShadowFrame
        {
            ShadowJobDesc job;
            ShadowHandle shadow;
            Vector3f position;
            float near_plane = 0.01f;
            float far_plane = 1.0f;
            std::array<Matrix4f, 6> face_view_projections{};
        };
        struct DirectionalShadowFrame
        {
            ShadowJobDesc job;
            ShadowHandle shadow;
            Vector3f light_direction;
            Matrix4f view;
            Matrix4f projection;
        };
        struct SpotShadowFrame
        {
            ShadowJobDesc job;
            ShadowHandle shadow;
            Vector3f position;
            Vector3f light_direction;
            float outer_cone_radians = 0.0f;
            float near_plane = 0.01f;
            float far_plane = 1.0f;
            Matrix4f view;
            Matrix4f projection;
        };

        void ConfigurePassSequence();
        std::optional<DirectionalShadowFrame> ScheduleDirectionalShadow(
            const std::vector<Light> &lights,
            const std::function<bool(ShadowHandle)> &is_shadow_handle_valid) const;
        std::optional<SpotShadowFrame> ScheduleSpotShadow(
            const std::vector<Light> &lights,
            const std::function<bool(ShadowHandle)> &is_shadow_handle_valid) const;
        std::optional<PointShadowFrame> SchedulePointShadow(
            const std::vector<Light> &lights,
            const std::function<bool(ShadowHandle)> &is_shadow_handle_valid) const;
        bool RecordDirectionalShadowPass();
        bool RecordSpotShadowPass();
        bool RecordPointShadowPass();
        bool RecordGBufferPass();
        bool RecordDeferredLightingPass();
        bool RecordToneMapPass();
        bool RecordCaptureViewPass(CaptureView view);
        bool ExecutePass(FixedRenderPassId id, const std::vector<Light> &lights);
        bool PrepareDirectionalShadowPassResources();
        bool GetPreparedProgram(
            BuiltInRenderAsset role,
            std::shared_ptr<const asset::ShaderProgramResource> &out_program) const;
        bool PrepareFullscreenPassResources();
        bool PrepareDeferredLightingPassResources();
        bool PrepareEnvironmentIbl(asset::AssetID source_asset,
                                   const data::TextureData &source,
                                   EnvironmentBindingBundle &bundle);
        bool EnsureEnvironmentFallbackBindings();
        bool ResolveLevelEnvironment(const EnvironmentSourceDesc &source,
                                     EnvironmentBindingBundle &bundle);
        bool PrepareGBufferDebugPassResources();
        bool PrepareToneMapPassResources();
        bool PrepareCaptureViewPassResources();
        void RecordShadowCaster(const MeshProxy &proxy,
                                const graphics::PerPassData &per_pass_data,
                                graphics::CommandRecorder &recorder);
        void RecordMeshProxy(const MeshProxy &proxy,
                             const graphics::PerPassData &per_pass_data,
                             graphics::CommandRecorder &recorder, MaterialPass pass);
        void UpdateEnvironment(const RenderSceneFrameInput &input);
        void ApplyPendingSceneRenderTargetExtent();

        graphics::RenderBackend *backend_ = nullptr;
        RenderResourceResolver *resource_resolver_ = nullptr;
        MaterialSystem *material_system_ = nullptr;
        const PreparedRenderAssetCatalog *prepared_assets_ = nullptr;
        RendererFrameTargets frame_targets_;
        std::optional<FixedRenderPassSequence> pass_sequence_;
        std::optional<FixedRenderPassFrame> active_pass_frame_;
        graphics::Extent2D pending_scene_render_target_extent_;
        FrameContext *active_frame_context_ = nullptr;
        const RenderWorld *render_world_ = nullptr;
        FrameLightingBinding frame_lighting_binding_;
        std::optional<DirectionalShadowFrame> active_directional_shadow_;
        std::optional<SpotShadowFrame> active_spot_shadow_;
        std::optional<PointShadowFrame> active_point_shadow_;
        bool spot_shadow_recorded_ = false;
        bool point_shadow_recorded_ = false;
        bool point_shadow_profile_logged_ = false;
        RenderCamera scene_camera_;
        std::optional<CaptureView> active_pending_capture_;
        graphics::PipelineHandle deferred_lighting_pipeline_;
        graphics::PipelineHandle gbuffer_debug_pipeline_;
        graphics::PipelineHandle capture_view_pipeline_;
        graphics::MeshHandle gbuffer_debug_fullscreen_mesh_;
        graphics::SamplerHandle gbuffer_debug_sampler_;
        graphics::SamplerHandle directional_shadow_sampler_;
        graphics::SamplerHandle spot_shadow_sampler_;
        graphics::SamplerHandle point_shadow_sampler_;
        EnvironmentBindingBundle level_environment_;
        EnvironmentBindingBundle active_environment_;
        std::optional<EnvironmentSourceHandle> failed_environment_source_;
        graphics::PipelineHandle tone_map_pipeline_;
        graphics::PipelineHandle directional_shadow_pipeline_;
    };
}

#endif
