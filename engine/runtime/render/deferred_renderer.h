#ifndef KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H
#define KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "asset/common.h"
#include "asset/shader.h"
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
#include "path_trace_probe_mode.h"
#include "path_trace_settings.h"
#include "ray_tracing_scene_sections.h"

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
        bool ray_tracing_enabled = true;
        bool path_tracing_enabled = true;
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
        void SetPathTraceSettings(const PathTraceSettings &settings);
        void InjectNextPathTraceDispatchFailure();
        void InvalidateRayTracingTextureBindings();
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
            uint64_t validity_stamp = 0;
            Vector3f position;
            float near_plane = 0.01f;
            float far_plane = 1.0f;
            std::array<Matrix4f, 6> face_view_projections{};
            std::vector<VisibleMeshSection> caster_candidates;
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
        RenderTarget *ResolveFrameTexture(GraphTextureHandle texture) const;
        RenderTarget *ResolveFrameTextureByName(std::string_view name) const;
        graphics::BufferHandle ResolveFrameBuffer(GraphBufferHandle buffer) const;
        graphics::AccelerationStructureHandle ResolveFrameAccelerationStructure(
            GraphAccelerationStructureHandle acceleration_structure) const;
        RenderTarget *ResolveNamedFrameTarget(std::string_view name);
        bool EnsurePathTraceHistoryTargets(uint32_t width, uint32_t height);
        uint64_t PathTraceHistorySignature(uint32_t width, uint32_t height) const;
        bool BuildFrameResourceBindings(const CompiledRenderGraph &plan);
        bool ValidatePassBindings(const CompiledRenderGraph::Pass &pass) const;
        // The description for a declared transient key, or null for one this
        // renderer does not implement.
        std::optional<graphics::RenderTargetDesc> DescribeFrameTransient(
            uint64_t key, const graphics::Extent2D &extent) const;
        // Acquires every transient the plan declares. False when one could not be
        // had, which fails the frame before any pass records.
        bool AcquireFrameTransients(const CompiledRenderGraph &plan);
        void ReleaseFrameTransients();
        RenderTarget *ResolvePassAttachment(const CompiledRenderGraph::Pass &pass);
        bool ApplyPassTransitions(const CompiledRenderGraph &plan,
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
        bool RecordRayTracingPathTracePass();
        bool RecordCaptureViewPass(CaptureView view);
        bool ExecutePass(FixedRenderPassId id, const std::vector<Light> &lights);
        bool PrepareRayTracingScene();
        bool RecordRayTracingBlasBuild();
        bool RecordRayTracingTlasBuild();
        void DestroyRayTracingPathTraceBindings();
        void DestroyRayTracingResources();
        bool PrepareDirectionalShadowPassResources();
        bool GetPreparedProgram(
            BuiltInRenderAsset role,
            std::shared_ptr<const asset::ShaderProgramResource> &out_program,
            asset::ShaderProgramVariant variant = asset::ShaderProgramVariant::Bound) const;
        bool GetPreparedRayTracingProgram(
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
        bool PrepareRayTracingPathTraceResources();
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
        std::array<std::optional<RenderGraphCompileResult>, 32> frame_plans_;
        std::optional<RenderGraphFrame> active_pass_frame_;
        // The plan the active frame executes, so the external terminal's state
        // requirements can be applied before the host's callback records.
        const CompiledRenderGraph *active_frame_plan_ = nullptr;
        // The frame's pooled transient, wrapped around a pool-owned handle.
        // RenderTarget is not movable, so the wrapper is held by pointer.
        std::unique_ptr<RenderTarget> transient_scene_hdr_;
        std::array<std::unique_ptr<RenderTarget>, 2> path_trace_history_targets_;
        std::unique_ptr<RenderTarget> path_trace_guide_target_;
        uint32_t path_trace_write_index_ = 0;
        uint32_t path_trace_sample_count_ = 0;
        uint64_t path_trace_history_signature_ = 0;
        uint64_t path_trace_shader_signature_ = 0;
        uint64_t tone_map_shader_signature_ = 0;
        PathTraceSettings requested_path_trace_settings_{};
        PathTraceSettings effective_path_trace_settings_{};
        std::string path_trace_settings_fallback_reason_;
        bool fail_next_path_trace_dispatch_ = false;
        struct FrameTextureBinding
        {
            GraphTextureHandle logical;
            std::string name;
            RenderTarget *physical = nullptr;
        };
        struct FrameBufferBinding
        {
            GraphBufferHandle logical;
            std::string name;
            graphics::BufferHandle physical;
        };
        struct FrameAccelerationStructureBinding
        {
            GraphAccelerationStructureHandle logical;
            std::string name;
            graphics::AccelerationStructureHandle physical;
        };
        // Explicit frame-local bindings keep physical resolution separate from
        // authored pass-resource identities and reject missing graph resources.
        std::vector<FrameTextureBinding> frame_texture_bindings_;
        std::vector<FrameBufferBinding> frame_buffer_bindings_;
        std::vector<FrameAccelerationStructureBinding> frame_acceleration_structure_bindings_;
        bool frame_execution_failed_ = false;
        bool frame_plan_valid_ = false;
        double frame_plan_compile_ms_ = 0.0;
        graphics::Extent2D pending_scene_render_target_extent_;
        FrameContext *active_frame_context_ = nullptr;
        const RenderWorld *render_world_ = nullptr;
        // Revision-stable section packets are shared by shadow scheduling,
        // shadow recording, and per-frame G-buffer visibility filtering.
        std::vector<MeshProxy> frame_render_world_snapshot_;
        std::vector<VisibleMeshSection> frame_section_packets_;
        uint64_t frame_section_packets_world_revision_ = 0;
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
        struct RayTracingBlasState
        {
            graphics::AccelerationStructureHandle handle;
            uint32_t geometry_count = 0;
            uint64_t geometry_signature = 0;
            bool built = false;
        };
        struct RayTracingMeshBuild
        {
            detail::RayTracingSectionKey key;
            graphics::AccelerationStructureHandle blas;
            std::size_t geometry_offset = 0;
            std::size_t geometry_count = 0;
            uint64_t geometry_signature = 0;
            bool needs_build = false;
        };
        struct RayTracingPathInstanceData
        {
            uint32_t geometry_offset = 0;
            uint32_t material_offset = 0;
            uint32_t geometry_count = 0;
        };
        struct RayTracingPathMaterialData
        {
            Vector4f base_color{0.72f, 0.72f, 0.72f, 1.0f};
            Vector4f emissive{0.0f, 0.0f, 0.0f, 1.0f};
            float metallic = 0.0f;
            float roughness = 1.0f;
            float normal_scale = 1.0f;
            uint32_t base_color_texture_index = 0xffffffffu;
            uint32_t metallic_texture_index = 0xffffffffu;
            uint32_t roughness_texture_index = 0xffffffffu;
            uint32_t metallic_channel = 0;
            uint32_t roughness_channel = 0;
        };
        struct RayTracingPathLightData
        {
            Vector4f position_or_type{};
            Vector4f direction_and_range{};
            Vector4f color_intensity{};
            Vector4f parameters{};
        };
        struct RayTracingPathTraceBindingCache
        {
            graphics::DescriptorSetHandle descriptor_set;
            graphics::RayTracingPipelineHandle pipeline;
            graphics::AccelerationStructureHandle top_level;
            graphics::RayTracingBufferReferenceTableHandle scene_table;
            graphics::TextureHandle hdr_output;
            graphics::TextureHandle history_output;
            graphics::TextureHandle guide_output;
            graphics::TextureHandle environment;
            graphics::SamplerHandle environment_sampler;
            UniformAllocation camera_uniform;
        };
        std::unordered_map<detail::RayTracingSectionKey, RayTracingBlasState,
                           detail::RayTracingSectionKeyHash> ray_tracing_blas_;
        graphics::AccelerationStructureHandle ray_tracing_tlas_;
        uint32_t ray_tracing_tlas_capacity_ = 0;
        bool ray_tracing_tlas_built_ = false;
        uint64_t ray_tracing_instance_signature_ = 0;
        uint64_t frame_ray_tracing_instance_signature_ = 0;
        uint64_t frame_ray_tracing_material_signature_ = 0;
        std::vector<graphics::RayTracingGeometryDesc> frame_ray_tracing_geometries_;
        std::vector<graphics::RayTracingInstanceDesc> frame_ray_tracing_instances_;
        std::vector<RayTracingPathInstanceData> frame_ray_tracing_instance_data_;
        std::vector<RayTracingPathMaterialData> frame_ray_tracing_material_data_;
        std::vector<RayTracingPathLightData> frame_ray_tracing_light_data_;
        std::vector<graphics::RayTracingBufferAddressPatch>
            frame_ray_tracing_scene_address_patches_;
        graphics::RayTracingBufferReferenceTableHandle ray_tracing_scene_table_;
        uint64_t ray_tracing_scene_cache_world_revision_ = 0;
        uint64_t ray_tracing_scene_cache_material_revision_ = 0;
        uint64_t ray_tracing_scene_cache_instance_signature_ = 0;
        uint64_t ray_tracing_scene_cache_material_signature_ = 0;
        uint64_t ray_tracing_scene_table_lighting_signature_ = 0;
        bool ray_tracing_scene_cache_path_tracing_enabled_ = false;
        bool ray_tracing_scene_cache_valid_ = false;
        bool frame_ray_tracing_scene_table_dirty_ = true;
        uint64_t ray_tracing_scene_record_cache_hits_total_ = 0;
        uint64_t ray_tracing_scene_record_cache_misses_total_ = 0;
        std::vector<std::array<RayTracingPathTraceBindingCache, 2>>
            ray_tracing_path_tracing_bindings_;
        uint64_t frame_ray_tracing_lighting_signature_ = 0;
        std::vector<RayTracingMeshBuild> frame_ray_tracing_mesh_builds_;
        std::vector<graphics::RayTracingBuildDesc> frame_ray_tracing_blas_builds_;
        std::vector<graphics::RayTracingBuildDesc> frame_ray_tracing_tlas_builds_;
        bool frame_ray_tracing_blas_build_ = false;
        bool frame_ray_tracing_tlas_build_ = false;
        uint64_t path_trace_scene_limit_diagnostic_signature_ = 0;
        bool spot_shadow_recorded_ = false;
        bool point_shadow_recorded_ = false;
        bool directional_shadow_cache_hit_ = false;
        bool directional_shadow_valid_ = false;
        uint64_t directional_shadow_stamp_ = 0;
        bool point_shadow_cache_hit_ = false;
        bool point_shadow_valid_ = false;
        uint64_t point_shadow_stamp_ = 0;
        uint64_t triangle_count_ = 0;
        RenderCamera scene_camera_;
        std::optional<CaptureView> active_pending_capture_;
        graphics::PipelineHandle deferred_lighting_pipeline_;
        graphics::PipelineHandle deferred_lighting_ray_query_pipeline_;
        bool ray_query_shadow_path_active_ = false;
        bool ray_tracing_enabled_ = true;
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
        graphics::RayTracingPipelineHandle ray_tracing_path_tracing_pipeline_;
        bool ray_tracing_path_tracing_available_ = false;
        bool path_tracing_enabled_ = true;
        bool active_ray_tracing_path_trace_ = false;
        RenderProfileSnapshot profile_;
        std::optional<size_t> active_profile_pass_;
    };
}

#endif
