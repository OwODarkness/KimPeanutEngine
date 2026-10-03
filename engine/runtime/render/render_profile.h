#ifndef KPENGINE_RUNTIME_RENDER_RENDER_PROFILE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_PROFILE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/type.h"
#include "graphics/backend/common/ray_tracing.h"
#include "path_trace_settings.h"
#include "render_graph/render_graph_frame.h"

namespace kpengine::render
{
    enum class RenderProfilePass : uint8_t
    {
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        GBuffer,
        ScreenSpaceAoEstimate,
        ScreenSpaceAoFilter,
        DeferredLighting,
        RayTracingPathTrace,
        ToneMap,
        RayTracingToneMap,
        CaptureView,
        DebugView,
        EditorComposite,
        RayTracingBlasBuild,
        RayTracingTlasBuild,
        Count,
    };

    constexpr uint32_t GetRenderGpuProfilePassId(const RenderProfilePass pass) noexcept
    {
        const char *name = nullptr;
        switch (pass)
        {
        case RenderProfilePass::DirectionalShadow: name = "render.directional_shadow"; break;
        case RenderProfilePass::SpotShadow: name = "render.spot_shadow"; break;
        case RenderProfilePass::PointShadow: name = "render.point_shadow"; break;
        case RenderProfilePass::GBuffer: name = "render.gbuffer"; break;
        case RenderProfilePass::ScreenSpaceAoEstimate: name = "render.ssao_estimate"; break;
        case RenderProfilePass::ScreenSpaceAoFilter: name = "render.ssao_filter"; break;
        case RenderProfilePass::DeferredLighting: name = "render.deferred_lighting"; break;
        case RenderProfilePass::RayTracingPathTrace: name = "render.ray_tracing_path_trace"; break;
        case RenderProfilePass::ToneMap: name = "render.tone_map"; break;
        case RenderProfilePass::RayTracingToneMap: name = "render.ray_tracing_tone_map"; break;
        case RenderProfilePass::CaptureView: name = "render.capture_view"; break;
        case RenderProfilePass::DebugView: name = "render.debug_view"; break;
        case RenderProfilePass::EditorComposite: name = "render.editor_composite"; break;
        case RenderProfilePass::RayTracingBlasBuild: name = "render.ray_tracing_blas_build"; break;
        case RenderProfilePass::RayTracingTlasBuild: name = "render.ray_tracing_tlas_build"; break;
        case RenderProfilePass::Count: return 0;
        }
        if (name == nullptr)
        {
            return 0;
        }
        uint32_t hash = 2166136261u;
        for (const char *cursor = name; *cursor != '\0'; ++cursor)
        {
            hash = (hash ^ static_cast<uint8_t>(*cursor)) * 16777619u;
        }
        return hash;
    }

    constexpr size_t GetRenderProfilePassIndex(const uint32_t gpu_profile_id) noexcept
    {
        for (size_t index = 0; index < static_cast<size_t>(RenderProfilePass::Count); ++index)
        {
            if (GetRenderGpuProfilePassId(static_cast<RenderProfilePass>(index)) == gpu_profile_id)
            {
                return index;
            }
        }
        return static_cast<size_t>(RenderProfilePass::Count);
    }

    constexpr bool RenderGpuProfileIdsAreUnique() noexcept
    {
        for (size_t left = 0; left < static_cast<size_t>(RenderProfilePass::Count); ++left)
        {
            for (size_t right = left + 1;
                 right < static_cast<size_t>(RenderProfilePass::Count); ++right)
            {
                if (GetRenderGpuProfilePassId(static_cast<RenderProfilePass>(left)) ==
                    GetRenderGpuProfilePassId(static_cast<RenderProfilePass>(right)))
                {
                    return false;
                }
            }
        }
        return true;
    }

    static_assert(RenderGpuProfileIdsAreUnique());

    enum class RenderProfileCpuSubphase : uint8_t
    {
        SectionPacketBuild,
        ShadowStampFit,
        MaterialResolution,
        UniformWrite,
        DescriptorSearch,
        DescriptorAllocation,
        DescriptorUpdate,
        PipelineValidation,
        GraphExecute,
        Count,
    };

    struct RenderProfilePassMetrics
    {
        double cpu_time_ms = 0.0;
        std::optional<double> gpu_time_ms;
        uint64_t draw_calls = 0;
        uint64_t sections = 0;
    };

    struct RenderProfilePassSummary
    {
        std::optional<double> gpu_p50_ms;
        std::optional<double> gpu_p95_ms;
    };

    struct RenderProfileCpuSubphaseSummary
    {
        std::optional<double> cpu_p50_ms;
        std::optional<double> cpu_p95_ms;
    };

    struct RenderProfileSummary
    {
        bool complete = false;
        uint32_t warmup_frames_completed = 0;
        uint32_t samples_collected = 0;
        double cpu_total_p50_ms = 0.0;
        double cpu_total_p95_ms = 0.0;
        double cpu_present_p50_ms = 0.0;
        double cpu_present_p95_ms = 0.0;
        double cpu_fence_wait_p50_ms = 0.0;
        double cpu_fence_wait_p95_ms = 0.0;
        double cpu_acquire_wait_p50_ms = 0.0;
        double cpu_acquire_wait_p95_ms = 0.0;
        double cpu_queue_present_p50_ms = 0.0;
        double cpu_queue_present_p95_ms = 0.0;
        std::optional<double> gpu_total_p50_ms;
        std::optional<double> gpu_total_p95_ms;
        std::array<RenderProfilePassSummary,
                   static_cast<size_t>(RenderProfilePass::Count)>
            passes{};
        std::array<RenderProfileCpuSubphaseSummary,
                   static_cast<size_t>(RenderProfileCpuSubphase::Count)>
            cpu_subphases{};
    };

    struct RenderProfileTextureMetrics
    {
        uint32_t dependency_count = 0;
        uint32_t tracked_residency_incomplete_count = 0;
        uint64_t source_bytes = 0;
        uint64_t decoded_bytes = 0;
        uint64_t resident_bytes = 0;
        bool tracked_residency_complete = true;
    };

    struct RenderGraphFailureSnapshot
    {
        bool valid = false;
        uint64_t frame_number = 0;
        uint32_t path_trace_samples = 0;
        std::array<RenderGraphPassOutcome,
                   static_cast<size_t>(RenderProfilePass::Count)>
            pass_outcomes{};
    };

    struct RenderProfileSnapshot
    {
        uint64_t frame_number = 0;
        uint32_t path_trace_samples = 0;
        uint32_t path_trace_samples_per_dispatch = 0;
        uint32_t path_trace_max_continuation_bounces = 0;
        PathTraceDirectLightSampling path_trace_direct_light_sampling =
            PathTraceDirectLightSampling::AllLights;
        PathTraceSettings path_trace_settings_requested{};
        PathTraceSettings path_trace_settings_effective{};
        std::string path_trace_sampling_state = "fixed";
        std::string path_trace_settings_fallback_reason;
        uint32_t ray_tracing_geometry_records = 0;
        uint32_t ray_tracing_instance_records = 0;
        uint32_t ray_tracing_material_records = 0;
        uint32_t ray_tracing_light_records = 0;
        uint64_t ray_tracing_scene_table_records_packed = 0;
        uint64_t ray_tracing_scene_table_records_uploaded = 0;
        uint64_t ray_tracing_scene_record_cache_hits_total = 0;
        uint64_t ray_tracing_scene_record_cache_misses_total = 0;
        uint64_t ray_tracing_descriptor_sets_created = 0;
        uint64_t ray_tracing_descriptor_pools_created = 0;
        uint64_t ray_tracing_address_table_buffers_created = 0;
        uint64_t ray_tracing_address_table_upload_bytes = 0;
        uint64_t ray_tracing_acceleration_structure_storage_bytes = 0;
        uint64_t ray_tracing_blas_builds = 0;
        uint64_t ray_tracing_blas_updates = 0;
        uint64_t ray_tracing_tlas_builds = 0;
        uint64_t ray_tracing_tlas_updates = 0;
        uint64_t ray_tracing_retired_acceleration_structures = 0;
        uint64_t ray_tracing_retired_descriptor_sets = 0;
        uint64_t ray_tracing_retired_temporary_buffer_batches = 0;
        std::array<graphics::RayTracingBuildDiagnostic,
                   graphics::kRayTracingBuildDiagnosticCapacity>
            ray_tracing_build_diagnostics{};
        uint32_t ray_tracing_build_diagnostic_count = 0;
        std::array<float, 3> path_trace_camera_position{};
        std::string render_graph_mode = "deferred";
        std::string path_trace_history_reset_reason = "none";
        bool path_trace_environment_enabled = false;
        double path_trace_environment_intensity = 0.0;
        bool ray_tracing_enabled = false;
        bool path_tracing_enabled = false;
        bool path_tracing_available = false;
        bool path_trace_active = false;
        bool ray_query_shadows_available = false;
        bool ray_query_shadows_active = false;
        bool screen_space_ao_requested = false;
        bool screen_space_ao_effective = false;
        float screen_space_ao_radius = 0.0f;
        float screen_space_ao_bias = 0.0f;
        float screen_space_ao_strength = 0.0f;
        uint32_t screen_space_ao_sample_count = 0;
        GraphicsAPIType graphics_api = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        uint32_t viewport_width = 0;
        uint32_t viewport_height = 0;
        double cpu_total_ms = 0.0;
        double cpu_scene_prepare_ms = 0.0;
        double cpu_render_world_snapshot_ms = 0.0;
        double cpu_ray_tracing_scene_prepare_ms = 0.0;
        double cpu_ray_tracing_scene_table_pack_ms = 0.0;
        double cpu_backend_begin_ms = 0.0;
        double cpu_record_ms = 0.0;
        double cpu_finalize_ms = 0.0;
        double cpu_present_ms = 0.0;
        double cpu_fence_wait_ms = 0.0;
        double cpu_acquire_wait_ms = 0.0;
        double cpu_queue_present_ms = 0.0;
        uint32_t gpu_timing_samples = 0;
        uint64_t draw_calls = 0;
        uint64_t sections = 0;
        uint64_t shadow_cache_hits = 0;
        uint64_t shadow_cache_misses = 0;
        uint64_t point_shadow_cache_hits = 0;
        uint64_t point_shadow_cache_misses = 0;
        uint64_t descriptor_sets_created = 0;
        uint64_t descriptor_pools_created = 0;
        double cpu_section_packet_build_ms = 0.0;
        uint64_t section_packet_build_calls = 0;
        uint64_t section_packets_built = 0;
        double cpu_shadow_stamp_fit_ms = 0.0;
        uint64_t shadow_stamp_evaluations = 0;
        uint64_t shadow_fit_evaluations = 0;
        double cpu_material_resolution_ms = 0.0;
        uint64_t material_resolution_calls = 0;
        double cpu_uniform_write_ms = 0.0;
        uint64_t uniform_writes = 0;
        uint64_t uniform_write_bytes = 0;
        double descriptor_search_cpu_ms = 0.0;
        double descriptor_allocation_cpu_ms = 0.0;
        double descriptor_update_cpu_ms = 0.0;
        uint64_t descriptor_searches = 0;
        uint64_t descriptor_allocations = 0;
        uint64_t descriptor_updates = 0;
        double pipeline_validation_cpu_ms = 0.0;
        uint64_t pipeline_validation_calls = 0;
        uint64_t pipeline_bind_requests = 0;
        uint64_t pipeline_bind_emitted = 0;
        uint64_t mesh_bind_requests = 0;
        uint64_t mesh_bind_emitted = 0;
        uint64_t resource_binding_bind_requests = 0;
        uint64_t resource_binding_bind_emitted = 0;
        uint64_t native_draw_calls = 0;
        // Graph cost is reported separately from pass cost: compilation is a
        // one-time per-variant cost, while the sweep runs every frame.
        double graph_compile_ms = 0.0;
        double cpu_graph_execute_ms = 0.0;
        std::string present_mode = "unknown";
        RenderProfileTextureMetrics textures;
        std::array<RenderProfilePassMetrics,
                   static_cast<size_t>(RenderProfilePass::Count)>
            passes{};
        std::array<RenderGraphPassOutcome,
                   static_cast<size_t>(RenderProfilePass::Count)>
            graph_pass_outcomes{};
        RenderGraphFailureSnapshot last_required_graph_failure{};
        std::optional<uint64_t> gpu_frame_number;
        RenderProfileSummary summary;
    };

    struct RenderProfileScenario
    {
        const char *name = "sponza-stage-0";
        const char *startup_level = "level/sponza.level";
        const char *camera_id = "main_camera";
        const char *build_type = "Debug";
        GraphicsAPIType graphics_api = GraphicsAPIType::GRAPHICS_API_VULKAN;
        uint32_t viewport_width = 1920;
        uint32_t viewport_height = 1080;
        uint32_t warmup_frames = 120;
        uint32_t sample_frames = 300;
    };

    constexpr RenderProfileScenario GetSponzaProfileScenario() noexcept
    {
        return {};
    }

    class RenderProfileWindow final
    {
    public:
        RenderProfileWindow(uint32_t warmup_frames, uint32_t sample_frames);

        void Observe(const RenderProfileSnapshot &snapshot);
        RenderProfileSummary GetSummary() const;
        void Reset();

    private:
        uint32_t warmup_frames_ = 0;
        uint32_t sample_frames_ = 0;
        uint32_t frames_observed_ = 0;
        std::vector<double> cpu_total_samples_;
        std::vector<double> cpu_present_samples_;
        std::vector<double> cpu_fence_wait_samples_;
        std::vector<double> cpu_acquire_wait_samples_;
        std::vector<double> cpu_queue_present_samples_;
        std::vector<double> gpu_total_samples_;
        std::array<std::vector<double>,
                   static_cast<size_t>(RenderProfileCpuSubphase::Count)>
            cpu_subphase_samples_;
        std::array<std::vector<double>, static_cast<size_t>(RenderProfilePass::Count)>
            gpu_samples_;
        mutable RenderProfileSummary cached_summary_{};
        mutable bool summary_dirty_ = true;
    };
}

#endif
