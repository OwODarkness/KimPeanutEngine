#ifndef KPENGINE_EDITOR_BUILTIN_METRICS_H
#define KPENGINE_EDITOR_BUILTIN_METRICS_H

#include <functional>
#include <utility>
#include "editor/profile/editor_metric.h"

namespace kpengine::editor
{
    // FPS from the engine, injected as a sampler so the metric never sees the engine.
    class EditorFPSMetric : public EditorMetric
    {
    public:
        explicit EditorFPSMetric(std::function<int()> fps_sampler);

        const char *Name() const override;
        std::string Sample() override;

    private:
        std::function<int()> fps_sampler_;
    };

    // Frame time in ms. Derived from the fps sampler by the caller (1000/fps): a
    // self-measured clock would see the render loop's pacing sleep, not frame cost.
    class EditorFrameTimeMetric : public EditorMetric
    {
    public:
        explicit EditorFrameTimeMetric(std::function<float()> ms_sampler,
                                       size_t history_capacity = 120);

        const char *Name() const override;
        std::string Sample() override;

    private:
        std::function<float()> ms_sampler_;
    };

    // Process + system memory. Measured by the runtime's platform layer
    // (platform/win MemoryStatsSampler) and consumed here as an injected sampler — the
    // metric never measures anything itself.
    class EditorMemoryMetric : public EditorMetric
    {
    public:
        using Stats = std::pair<double, double>; // (process_mb, system_available_mb)

        explicit EditorMemoryMetric(std::function<Stats()> stats_sampler,
                                    size_t history_capacity = 120);

        const char *Name() const override;
        std::string Sample() override;

    private:
        std::function<Stats()> stats_sampler_;
    };
}

#endif // KPENGINE_EDITOR_BUILTIN_METRICS_H
