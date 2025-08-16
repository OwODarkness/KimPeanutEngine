#include "editor_log_component.h"

#include <string>
#include <vector>
#include "runtime/core/system/log_system.h"
#include "runtime/core/log/logger.h"
namespace kpengine
{
    namespace ui
    {

    void ExtractTipColorFromLogLevel(float (&color)[4], program::LogLevel level)
    {
        switch (level)
        {
        case program::LogLevel::Debug:
        case program::LogLevel::Info:
            color[0] = 1.f;
            color[1] = 1.f;
            color[2] = 1.f;
            color[3] = 1.f;
            break;
        case program::LogLevel::Warning:
            color[0] = 1.f;
            color[1] = 1.f;
            color[2] = 0.f;
            color[3] = 1.f;
            break;
        case program::LogLevel::Error:
            color[0] = 0.5f;
            color[1] = 0.2f;
            color[2] = 0.f;
            color[3] = 1.f;
        case program::LogLevel::Fatal:
            color[0] = 1.f;
            color[1] = 0.f;
            color[2] = 0.f;
            color[3] = 1.f;
            break;
        default:
            break;
        }
    }


        EditorLogComponent::EditorLogComponent(LogSystem *log_system) : EditorWindowComponent("OutputLog"),
                                                                        log_system_(log_system) {}

        void EditorLogComponent::RenderContent()
        {
            EditorWindowComponent::RenderContent();
            assert(log_system_ != nullptr);
            const std::vector<program::LogEntry> &logs = log_system_->GetLogs();
            for (const auto& log : logs)
            {
                float color[4];
                ExtractTipColorFromLogLevel(color, log.level);
                ImVec4 imgui_color(color[0], color[1], color[2], color[3]);
                ImGui::TextColored(imgui_color, program::Logger::FetchStringFromLog(log).c_str());
            }
        }

    }
}