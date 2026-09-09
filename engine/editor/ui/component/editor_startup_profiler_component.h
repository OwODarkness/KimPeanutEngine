#ifndef KPENGINE_EDITOR_STARTUP_PROFILER_COMPONENT_H
#define KPENGINE_EDITOR_STARTUP_PROFILER_COMPONENT_H

#include <functional>
#include <cstdint>

#include "editor/ui/component/editor_window_component.h"
#include "runtime/runtime_startup.h"

namespace kpengine
{
    class MemoryStatsSampler;
}

namespace kpengine::editor
{
    enum class StartupProfilerSortMode : uint8_t
    {
        Completion,
        TotalTime,
        SourceTime,
        Memory,
    };

    class EditorStartupProfilerComponent final : public EditorWindowComponent
    {
    public:
        EditorStartupProfilerComponent(
            std::function<runtime::StartupSnapshot()> snapshot_source,
            MemoryStatsSampler *memory_sampler);

        void RenderContent() override;

        const runtime::StartupSnapshot &GetLastSnapshot() const noexcept
        {
            return last_snapshot_;
        }

        double GetPeakProcessMemoryMb() const noexcept
        {
            return peak_process_memory_mb_;
        }

    private:
        std::function<runtime::StartupSnapshot()> snapshot_source_;
        MemoryStatsSampler *memory_sampler_ = nullptr;
        runtime::StartupSnapshot last_snapshot_{};
        double peak_process_memory_mb_ = 0.0;
        StartupProfilerSortMode sort_mode_ = StartupProfilerSortMode::TotalTime;
    };
}

#endif // KPENGINE_EDITOR_STARTUP_PROFILER_COMPONENT_H
