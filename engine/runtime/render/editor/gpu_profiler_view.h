#ifndef KPENGINE_RENDER_EDITOR_GPU_PROFILER_VIEW_H
#define KPENGINE_RENDER_EDITOR_GPU_PROFILER_VIEW_H

#include "editor_ui/ui_backend.h"

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::render::editor
{
    struct SceneEditorTimingSnapshot
    {
        double frame_total_ms = 0.0;
        double game_wait_ms = 0.0;
        double render_work_ms = 0.0;
        double frame_pacing_ms = 0.0;
        double game_tick_work_ms = 0.0;
        double game_tick_pacing_ms = 0.0;
        double ui_total_ms = 0.0;
        double ui_build_ms = 0.0;
        double ui_submit_ms = 0.0;
    };

    class GpuProfilerView final
    {
    public:
        explicit GpuProfilerView(RenderSystem *render_system) noexcept;
        void Draw(kpengine::editor::ui::UiContext &context, const SceneEditorTimingSnapshot &timings) const;

    private:
        RenderSystem *render_system_ = nullptr;
    };
}

#endif
