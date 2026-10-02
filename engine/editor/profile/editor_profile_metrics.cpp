#include "editor/profile/editor_profile_metrics.h"

#include <cstdio>
#include <memory>

#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_metric.h"
#include "platform/memory_stats_sampler.h"
#include "runtime/engine.h"
#include "runtime/render/render_system.h"

namespace kpengine::editor
{
    std::vector<std::unique_ptr<EditorMetric>> CreateEditorProfileMetrics(
        runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
        render::RenderSystem *render_system)
    {
        std::vector<std::unique_ptr<EditorMetric>> metrics;
        if (engine == nullptr || memory_sampler == nullptr || render_system == nullptr)
        {
            return metrics;
        }

        metrics.push_back(std::make_unique<EditorFPSMetric>(
            [engine] { return engine->GetFPS(); }));
        metrics.push_back(std::make_unique<EditorFrameTimeMetric>(
            [engine]
            {
                const int fps = engine->GetFPS();
                return fps > 0 ? 1000.f / static_cast<float>(fps) : 0.f;
            }));
        metrics.push_back(std::make_unique<EditorFuncMetric>(
            "CPU",
            [render_system]
            {
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.2f ms",
                              render_system->GetMetrics().profile.cpu_total_ms);
                return std::string{value};
            }));
        metrics.push_back(std::make_unique<EditorFuncMetric>(
            "GPU",
            [render_system]
            {
                const auto profile = render_system->GetMetrics().profile;
                double total = 0.0;
                bool measured = false;
                for (const auto &pass : profile.passes)
                {
                    if (pass.gpu_time_ms.has_value())
                    {
                        total += *pass.gpu_time_ms;
                        measured = true;
                    }
                }
                if (!measured)
                {
                    return std::string{"N/A"};
                }
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.2f ms", total);
                return std::string{value};
            }));
        metrics.push_back(std::make_unique<EditorFuncMetric>(
            "API",
            [render_system]
            {
                switch (render_system->GetMetrics().profile.graphics_api)
                {
                case GraphicsAPIType::GRAPHICS_API_OPENGL:
                    return std::string{"OpenGL"};
                case GraphicsAPIType::GRAPHICS_API_VULKAN:
                    return std::string{"Vulkan"};
                case GraphicsAPIType::GRAPHICS_API_UNKNOW:
                default:
                    return std::string{"Unknown"};
                }
            }));
        metrics.push_back(std::make_unique<EditorFuncMetric>(
            "GPU use",
            [render_system]
            {
                const auto usage = render_system->GetMetrics().gpu_usage_percent;
                if (!usage.has_value())
                {
                    return std::string{"N/A"};
                }
                char value[24]{};
                std::snprintf(value, sizeof(value), "%.0f%%", *usage);
                return std::string{value};
            }));
        metrics.push_back(std::make_unique<EditorMemoryMetric>(
            [memory_sampler]() -> EditorMemoryMetric::Stats
            {
                const MemoryStats stats = memory_sampler->Sample();
                return {stats.process_mb, stats.system_available_mb};
            }));
        return metrics;
    }
}
