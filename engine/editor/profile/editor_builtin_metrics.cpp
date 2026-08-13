#include "editor/profile/editor_builtin_metrics.h"

#include <cstdio>
#include <utility>

namespace kpengine::editor
{
    EditorFPSMetric::EditorFPSMetric(std::function<int()> fps_sampler)
        : fps_sampler_(std::move(fps_sampler))
    {
    }

    const char *EditorFPSMetric::Name() const
    {
        return "FPS";
    }

    std::string EditorFPSMetric::Sample()
    {
        return std::to_string(fps_sampler_());
    }

    EditorFrameTimeMetric::EditorFrameTimeMetric(std::function<float()> ms_sampler,
                                                 size_t history_capacity)
        : EditorMetric(history_capacity), ms_sampler_(std::move(ms_sampler))
    {
    }

    const char *EditorFrameTimeMetric::Name() const
    {
        return "Frame";
    }

    std::string EditorFrameTimeMetric::Sample()
    {
        const float ms = ms_sampler_();
        if (HasPlot())
        {
            RecordPlotValue(ms);
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f ms", ms);
        return buffer;
    }

    EditorMemoryMetric::EditorMemoryMetric(std::function<Stats()> stats_sampler,
                                           size_t history_capacity)
        : EditorMetric(history_capacity), stats_sampler_(std::move(stats_sampler))
    {
    }

    const char *EditorMemoryMetric::Name() const
    {
        return "Mem";
    }

    std::string EditorMemoryMetric::Sample()
    {
        const Stats stats = stats_sampler_();
        const double process_mb = stats.first;
        const double available_mb = stats.second;
        if (HasPlot())
        {
            RecordPlotValue(static_cast<float>(process_mb));
        }
        char buffer[64];
        if (available_mb >= 1024.0)
        {
            std::snprintf(buffer, sizeof(buffer), "%.0f MB / %.1f GB free", process_mb,
                          available_mb / 1024.0);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.0f MB / %.0f MB free", process_mb,
                          available_mb);
        }
        return buffer;
    }
}
