#ifndef KPENGINE_RUNTIME_RENDER_RENDER_PROFILE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_PROFILE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/type.h"

namespace kpengine::render
{
    enum class RenderProfilePass : uint8_t
    {
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        GBuffer,
        DeferredLighting,
        ToneMap,
        CaptureView,
        EditorComposite,
        Count,
    };

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
        uint64_t source_bytes = 0;
        uint64_t decoded_bytes = 0;
        uint64_t resident_bytes = 0;
    };

    struct RenderProfileSnapshot
    {
        uint64_t frame_number = 0;
        GraphicsAPIType graphics_api = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        uint32_t viewport_width = 0;
        uint32_t viewport_height = 0;
        double cpu_total_ms = 0.0;
        double cpu_scene_prepare_ms = 0.0;
        double cpu_backend_begin_ms = 0.0;
        double cpu_record_ms = 0.0;
        double cpu_finalize_ms = 0.0;
        double cpu_present_ms = 0.0;
        uint64_t draw_calls = 0;
        uint64_t sections = 0;
        uint64_t shadow_cache_hits = 0;
        uint64_t shadow_cache_misses = 0;
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
        std::string present_mode = "unknown";
        RenderProfileTextureMetrics textures;
        std::array<RenderProfilePassMetrics,
                   static_cast<size_t>(RenderProfilePass::Count)>
            passes{};
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
        std::array<std::vector<double>,
                   static_cast<size_t>(RenderProfileCpuSubphase::Count)>
            cpu_subphase_samples_;
        std::array<std::vector<double>, static_cast<size_t>(RenderProfilePass::Count)>
            gpu_samples_;
    };
}

#endif
