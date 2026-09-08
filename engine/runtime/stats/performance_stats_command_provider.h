#ifndef KPENGINE_RUNTIME_STATS_PERFORMANCE_STATS_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_STATS_PERFORMANCE_STATS_COMMAND_PROVIDER_H

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "command/command_registry.h"
#include "render/render_profile.h"

namespace kpengine::runtime
{
    struct PerformanceStatsFrameMetrics
    {
        double frame_total_ms = 0.0;
        double game_wait_ms = 0.0;
        double render_work_ms = 0.0;
        double frame_pacing_ms = 0.0;
        double game_tick_work_ms = 0.0;
        double game_tick_pacing_ms = 0.0;
    };

    struct PerformanceStatsSnapshot
    {
        render::RenderProfileSnapshot profile;
        PerformanceStatsFrameMetrics frame_loop;
        uint64_t triangle_count = 0;
        std::optional<float> gpu_usage_percent;
    };

    using PerformanceStatsSnapshotProvider = std::function<PerformanceStatsSnapshot()>;

    struct PerformanceStatsCommandRegistrationResult
    {
        std::array<command::CommandRegistration, 3> registrations;
        command::CommandRegistrationStatus status =
            command::CommandRegistrationStatus::InvalidDescriptor;
        std::string diagnostic;

        bool IsSuccess() const noexcept;
    };

    PerformanceStatsCommandRegistrationResult RegisterPerformanceStatsCommands(
        command::CommandRegistry &registry, PerformanceStatsSnapshotProvider provider);
}

#endif
