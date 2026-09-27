#include "editor/log/editor_log_component.h"

#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/core/log/log_system.h"
#include "runtime/core/log/logger.h"

namespace kpengine::editor
{
    namespace
    {
        struct LogLevelFilterOption
        {
            const char *label;
            bool matches_all;
            program::LogLevel level;
        };

        constexpr LogLevelFilterOption kLogLevelFilters[] = {
            {"All levels", true, program::LogLevel::Debug},
            {"Debug", false, program::LogLevel::Debug},
            {"Info", false, program::LogLevel::Info},
            {"Warning", false, program::LogLevel::Warning},
            {"Error", false, program::LogLevel::Error},
            {"Fatal", false, program::LogLevel::Fatal},
        };

        bool ContainsCaseInsensitive(const std::string_view text,
                                     const std::string_view substring)
        {
            if (substring.empty())
            {
                return true;
            }
            if (substring.size() > text.size())
            {
                return false;
            }

            return std::search(text.begin(), text.end(), substring.begin(), substring.end(),
                               [](const char left, const char right)
                               {
                                   return std::tolower(static_cast<unsigned char>(left)) ==
                                          std::tolower(static_cast<unsigned char>(right));
                               }) != text.end();
        }

        bool MatchesSearch(const program::LogEntry &log, const std::string_view query)
        {
            const LogLevelFilterOption *level_filter = nullptr;
            for (const LogLevelFilterOption &option : kLogLevelFilters)
            {
                if (!option.matches_all && option.level == log.level)
                {
                    level_filter = &option;
                    break;
                }
            }

            return ContainsCaseInsensitive(log.name, query) ||
                   ContainsCaseInsensitive(log.message, query) ||
                   ContainsCaseInsensitive(log.file, query) ||
                   (level_filter != nullptr && ContainsCaseInsensitive(level_filter->label, query));
        }
    }

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

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("##log-level-filter",
                              kLogLevelFilters[level_filter_index_].label))
        {
            for (std::size_t index = 0; index < std::size(kLogLevelFilters); ++index)
            {
                const bool selected = index == level_filter_index_;
                if (ImGui::Selectable(kLogLevelFilters[index].label, selected))
                {
                    level_filter_index_ = index;
                    jump_to_latest_ = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputTextWithHint("##log-search", "Search logs...",
                                     search_buffer_.data(), search_buffer_.size()))
        {
            jump_to_latest_ = true;
        }

        const std::string_view search_query(search_buffer_.data());
        std::vector<const program::LogEntry *> filtered_logs;
        filtered_logs.reserve(logs.size());
        for (const program::LogEntry &log : logs)
        {
            const LogLevelFilterOption &filter = kLogLevelFilters[level_filter_index_];
            if ((!filter.matches_all && filter.level != log.level) ||
                !MatchesSearch(log, search_query))
            {
                continue;
            }
            filtered_logs.push_back(&log);
        }

        ImGui::SameLine();
        const std::string copy_label = "Copy filtered (" +
            std::to_string(filtered_logs.size()) + ")";
        if (ImGui::Button(copy_label.c_str()))
        {
            std::string clipboard_text;
            for (const program::LogEntry *const log : filtered_logs)
            {
                clipboard_text += program::Logger::FetchStringFromLog(*log);
                clipboard_text.push_back('\n');
            }
            ImGui::SetClipboardText(clipboard_text.c_str());
        }

        if (filtered_logs.empty())
        {
            ImGui::TextDisabled(logs.empty() ? "No logs" : "No matching logs");
            last_log_count_ = logs.size();
            jump_to_latest_ = false;
            return;
        }

        // Virtualized: render + format only the rows visible in the scroll window.
        // Logs grow past the viewport (there's a scrollbar), so walking every entry
        // each frame would re-run the timestamp/level formatting below on all of them.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered_logs.size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const program::LogEntry &log = *filtered_logs[static_cast<size_t>(i)];
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
