#ifndef KPENGINE_EDITOR_PROFILE_METRICS_H
#define KPENGINE_EDITOR_PROFILE_METRICS_H

#include <memory>
#include <vector>

namespace kpengine
{
    class MemoryStatsSampler;
    namespace runtime
    {
        class Engine;
    }
    namespace render
    {
        class RenderSystem;
    }
}

namespace kpengine::editor
{
    class EditorMetric;

    std::vector<std::unique_ptr<EditorMetric>> CreateEditorProfileMetrics(
        runtime::Engine *engine, MemoryStatsSampler *memory_sampler,
        render::RenderSystem *render_system);
}

#endif // KPENGINE_EDITOR_PROFILE_METRICS_H
