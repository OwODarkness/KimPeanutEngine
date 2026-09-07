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
        std::array<std::vector<double>, static_cast<size_t>(RenderProfilePass::Count)>
            gpu_samples_;
    };
}

#endif
