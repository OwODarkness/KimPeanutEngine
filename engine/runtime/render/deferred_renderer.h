#ifndef KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H
#define KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
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
#include "render_graph/render_graph_frame.h"
#include "render_pass_declaration.h"
#include "render_resource.h"
#include "prepared_render_asset_catalog.h"
#include "render_world/render_world.h"
#include "render_world/scene_visibility.h"
#include "renderer_frame_targets.h"
#include "render_profile.h"

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
        spatial::Ray BuildSceneRay(float ndc_x, float ndc_y,
                                   float viewport_aspect) const;
        std::optional<Vector3f> ProjectScenePoint(const Vector3f &world_point,
                                                   float viewport_aspect) const;
        graphics::RenderTargetView GetViewportRenderTargetView(CaptureView view) const;
        graphics::RenderTargetHandle GetCaptureTarget(CaptureView view) const;
        uint64_t GetTriangleCount() const { return triangle_count_; }
        RenderProfileSnapshot GetProfileSnapshot() const { return profile_; }
        bool IsFramePlanValid() const { return frame_plan_valid_; }

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
            uint64_t validity_stamp = 0;
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

        // Applies the state requirements the plan records for one pass, before
        // the pass records anything. The plan owns what state a resource must be
        // in; the backend owns what it is currently in and elides what is
        // already satisfied.
        // The target a pass records into, named by its write use. Null for a pass
        // that writes no attachment, such as the external terminal.
        // The target backing a logical resource. Everything except SceneHdr is a
        // persistent frame target; SceneHdr is the frame's transient, taken from
        // the Graphics-owned pool.
        RenderTarget *ResolveResourceTarget(RenderPassResource resource);
        // The description for a declared transient key, or null for one this
        // renderer does not implement.
        std::optional<graphics::RenderTargetDesc> DescribeFrameTransient(
            uint64_t key, const graphics::Extent2D &extent) const;
        // Acquires every transient the plan declares. False when one could not be
        // had, which fails the frame before any pass records.
        bool AcquireFrameTransients(const CompiledRenderGraph &plan);
        void ReleaseFrameTransients();
        RenderTarget *ResolvePassAttachment(const CompiledRenderGraph::Pass &pass);
        void ApplyPassTransitions(const CompiledRenderGraph &plan,
                                  const CompiledRenderGraph::Pass &pass);
        void ConfigureFramePlans();
        const CompiledRenderGraph *GetFramePlan(RenderFrameConditions conditions) const;
        std::optional<DirectionalShadowFrame> ScheduleDirectionalShadow(
            const std::vector<Light> &lights,
            const std::function<bool(ShadowHandle)> &is_shadow_handle_valid);
        std::optional<SpotShadowFrame> ScheduleSpotShadow(
            const std::vector<Light> &lights,
            const std::function<bool(ShadowHandle)> &is_shadow_handle_valid);
        std::optional<PointShadowFrame> SchedulePointShadow(
            const std::vector<Light> &lights,
            const std::function<bool(ShadowHandle)> &is_shadow_handle_valid);
        const std::vector<VisibleMeshSection> &BuildSectionCandidatesProfiled();
        std::vector<VisibleMeshSection> BuildVisibleSectionsProfiled(
            const Matrix4f &view_projection);
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
                                const UniformAllocation &per_pass,
                                graphics::CommandRecorder &recorder,
                                uint32_t section_index = std::numeric_limits<uint32_t>::max());
        bool RecordMeshProxy(const MeshProxy &proxy,
                             const UniformAllocation &per_pass,
                             graphics::CommandRecorder &recorder, MaterialPass pass,
                             uint32_t section_index = std::numeric_limits<uint32_t>::max());
        void UpdateEnvironment(const RenderSceneFrameInput &input);
        void ApplyPendingSceneRenderTargetExtent();
        void AddProfileDraws(uint64_t draw_calls, uint64_t sections);

        graphics::RenderBackend *backend_ = nullptr;
        RenderResourceResolver *resource_resolver_ = nullptr;
        MaterialSystem *material_system_ = nullptr;
        const PreparedRenderAssetCatalog *prepared_assets_ = nullptr;
        RendererFrameTargets frame_targets_;
        // One compiled plan per frame-start condition set, each compiled once.
        // The compiled plan is now the only authority for pass order.
        std::array<std::optional<RenderGraphCompileResult>, 2> frame_plans_;
        std::optional<RenderGraphFrame> active_pass_frame_;
        // The plan the active frame executes, so the external terminal's state
        // requirements can be applied before the host's callback records.
        const CompiledRenderGraph *active_frame_plan_ = nullptr;
        // The frame's pooled transient, wrapped around a pool-owned handle.
        // RenderTarget is not movable, so the wrapper is held by pointer.
        std::unique_ptr<RenderTarget> transient_scene_hdr_;
        bool frame_plan_valid_ = false;
        double frame_plan_compile_ms_ = 0.0;
        graphics::Extent2D pending_scene_render_target_extent_;
        FrameContext *active_frame_context_ = nullptr;
        const RenderWorld *render_world_ = nullptr;
        // One immutable, frame-local section packet snapshot is shared by
        // shadow scheduling, shadow recording, and G-buffer visibility.
        std::vector<MeshProxy> frame_render_world_snapshot_;
        std::vector<VisibleMeshSection> frame_section_packets_;
        bool frame_section_packets_ready_ = false;
        FrameLightingBinding frame_lighting_binding_;
        // Per-frame resolved draw state. Within one frame the per-object uniform
        // and the material binding are pure functions of the renderable and its
        // material, so a section whose mesh was already drawn reuses the
        // resolution instead of repeating the lookups, hashing, and uniform
        // write. Both are cleared at frame start.
        struct FrameObjectState
        {
            UniformAllocation per_object;
            UniformAllocation selection;
        };
        std::unordered_map<uint64_t, FrameObjectState> frame_object_states_;
        std::unordered_map<uint64_t, FrameMaterialBinding> frame_material_bindings_;
        std::optional<DirectionalShadowFrame> active_directional_shadow_;
        std::optional<SpotShadowFrame> active_spot_shadow_;
        std::optional<PointShadowFrame> active_point_shadow_;
        bool spot_shadow_recorded_ = false;
        bool point_shadow_recorded_ = false;
        bool directional_shadow_cache_hit_ = false;
        bool directional_shadow_valid_ = false;
        uint64_t directional_shadow_stamp_ = 0;
        uint64_t triangle_count_ = 0;
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
        RenderProfileSnapshot profile_;
        std::optional<size_t> active_profile_pass_;
    };
}

#endif
