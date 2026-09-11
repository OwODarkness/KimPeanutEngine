#include "editor/log/editor_log_component.h"

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

        // LogSystem is a stateless facade over the process-global logger, so a
        // null one (hosts that never initialize scene services) still has real
        // logs to show. Snapshot under the logger's mutex — never iterate the
        // live vector while a writer thread pushes/clears it.
        const std::vector<program::LogEntry> logs =
            log_system_ != nullptr ? log_system_->GetLogSnapshot()
                                   : program::Logger::GetLogger().GetSnapshot();

        if (ImGui::Checkbox("Follow latest", &follow_latest_) && follow_latest_)
        {
            jump_to_latest_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Latest"))
        {
            jump_to_latest_ = true;
        }

        if (logs.empty())
        {
            last_log_count_ = 0;
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

        const bool logs_grew = logs.size() > last_log_count_;
        if (jump_to_latest_ || (follow_latest_ && logs_grew))
        {
            // The clipper has submitted the visible rows, so ImGui can resolve
            // the final scroll range after this request.
            ImGui::SetScrollHereY(1.0f);
        }
        jump_to_latest_ = false;
        last_log_count_ = logs.size();
    }

}
