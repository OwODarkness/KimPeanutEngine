#ifndef KPENGINE_EDITOR_GPU_PROFILER_COMPONENT_H
#define KPENGINE_EDITOR_GPU_PROFILER_COMPONENT_H

#include "editor/ui/component/editor_window_component.h"

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::editor
{
    class EditorUI;

    // Compact read-only view of Render's completed GPU pass timestamps and
    // per-frame geometry counters.
    class EditorGpuProfilerComponent final : public EditorWindowComponent
    {
    public:
        EditorGpuProfilerComponent(runtime::Engine *engine,
                                   render::RenderSystem *render_system,
                                   const EditorUI *editor_ui);

        void RenderContent() override;

    private:
        runtime::Engine *engine_ = nullptr;
        render::RenderSystem *render_system_ = nullptr;
        const EditorUI *editor_ui_ = nullptr;
    };
}

#endif
