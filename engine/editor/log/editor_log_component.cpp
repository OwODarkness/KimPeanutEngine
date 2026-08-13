#include "editor/log/editor_log_component.h"

#include <cassert>
#include <string>
#include <vector>
#include "runtime/core/log/log_system.h"
#include "runtime/core/log/logger.h"
namespace kpengine::editor
{

    EditorLogComponent::EditorLogComponent(LogSystem *log_system, const LogLevelColorTable &colors)
        : EditorWindowComponent("OutputLog"),
          log_system_(log_system), colors_(colors) {}

    void EditorLogComponent::RenderContent()
    {
        EditorWindowComponent::RenderContent();
        assert(log_system_ != nullptr);
        const std::vector<program::LogEntry> &logs = log_system_->GetLogs();
        for (const auto &log : logs)
        {
            // Colors come from config/settings.json (loaded by EditorUI, with
            // defaults as fallback); index by level instead of switching on it.
            const LogColor &color = colors_[static_cast<size_t>(log.level)];
            ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a),
                               program::Logger::FetchStringFromLog(log).c_str());
        }
    }

}