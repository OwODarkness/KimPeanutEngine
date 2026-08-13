#include "editor/log/editor_log_component.h"

#include <cassert>
#include <imgui.h>
#include <vector>
#include "runtime/core/log/log_system.h"
#include "runtime/core/log/logger.h"
namespace kpengine::editor
{

    EditorLogComponent::EditorLogComponent(LogSystem *log_system, const LogLevelColorTable &colors,
                                           EditorWindowConfig config)
        : EditorWindowComponent("OutputLog", config),
          log_system_(log_system), colors_(colors) {}

    void EditorLogComponent::RenderContent()
    {
        EditorWindowComponent::RenderContent();
        assert(log_system_ != nullptr);

        // Snapshot under the logger's mutex — never iterate the live vector while a
        // writer thread pushes/clears it (the render thread and writers race).
        const std::vector<program::LogEntry> logs = log_system_->GetLogSnapshot();
        if (logs.empty())
        {
            return;
        }

        // Virtualized: render + format only the rows visible in the scroll window.
        // Logs grow past the viewport (there's a scrollbar), so walking every entry
        // each frame would re-run the timestamp/level formatting below on all of them.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(logs.size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const program::LogEntry &log = logs[static_cast<size_t>(i)];
                // Colors come from config/settings.json (loaded by EditorUI, with
                // defaults as fallback); index by level instead of switching on it.
                const LogColor &color = colors_[static_cast<size_t>(log.level)];
                // "%s": pass the message as data, never as the format string — a log
                // line containing '%' must not be re-parsed.
                ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a), "%s",
                                   program::Logger::FetchStringFromLog(log).c_str());
            }
        }
    }

}