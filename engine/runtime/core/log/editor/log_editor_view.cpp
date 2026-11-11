#include "log_editor_view.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "editor_ui/ui_backend.h"
#include "runtime/core/log/log_system.h"
#include "runtime/core/log/logger.h"

namespace kpengine::editor
{
    namespace
    {
        struct LogLevelFilterOption
        {
            const char *key;
            const char *label;
            bool matches_all;
            program::LogLevel level;
        };

        constexpr LogLevelFilterOption kLogLevelFilters[] = {
            {"all", "All levels", true, program::LogLevel::Debug},
            {"debug", "Debug", false, program::LogLevel::Debug},
            {"info", "Info", false, program::LogLevel::Info},
            {"warning", "Warning", false, program::LogLevel::Warning},
            {"error", "Error", false, program::LogLevel::Error},
            {"fatal", "Fatal", false, program::LogLevel::Fatal},
        };

        const LogLevelFilterOption &FindFilter(const std::string_view key)
        {
            const auto found = std::find_if(std::begin(kLogLevelFilters),
                                            std::end(kLogLevelFilters),
                [key](const LogLevelFilterOption &option) { return option.key == key; });
            return found == std::end(kLogLevelFilters) ? kLogLevelFilters[0] : *found;
        }

        bool ContainsCaseInsensitive(const std::string_view text,
                                     const std::string_view substring)
        {
            if (substring.empty())
                return true;
            if (substring.size() > text.size())
                return false;
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

        ui::UiTheme LogRowTheme(const LogEditorColor &color)
        {
            ui::UiTheme theme{};
            theme.text = {color.r, color.g, color.b, color.a};
            return theme;
        }
    }

    LogEditorView::LogEditorView()
        : LogEditorView(LogEditorColorTable{{
              {0.8431373F, 0.9764706F, 1.0F, 1.0F},
              {0.2235294F, 1.0F, 0.5333334F, 1.0F},
              {0.0F, 0.8980392F, 1.0F, 1.0F},
              {1.0F, 0.3019608F, 0.5529412F, 1.0F},
              {1.0F, 0.3019608F, 0.5529412F, 1.0F}}})
    {
    }

    LogEditorView::LogEditorView(LogEditorColorTable colors)
        : colors_(colors)
    {
    }

    void LogEditorView::RenderContent(ui::UiContext &ui, LogSystem *log_system)
    {
        const std::vector<program::LogEntry> logs =
            log_system != nullptr ? log_system->GetLogSnapshot()
                                  : program::Logger::GetLogger().GetSnapshot();

        const auto follow = ui.Checkbox({.id = "log.follow_latest", .label = "Follow latest"},
                                        follow_latest_);
        if (follow.changed)
        {
            follow_latest_ = follow.value;
            jump_to_latest_ = follow_latest_;
        }
        ui.SameLine();
        if (ui.Button({.id = "log.latest", .label = "Latest"}).activated)
        {
            follow_latest_ = true;
            jump_to_latest_ = true;
        }

        std::vector<ui::UiChoice> filter_choices;
        for (const LogLevelFilterOption &option : kLogLevelFilters)
            filter_choices.push_back({option.key, option.label, ui::UiValue{std::string{option.key}}});
        const auto filter = ui.Combo<std::string>(
            {.id = "log.level", .label = "Level"}, level_filter_, std::move(filter_choices));
        if (filter.changed)
        {
            level_filter_ = filter.value;
            selected_log_index_ = -1;
            jump_to_latest_ = true;
        }

        ui.SameLine();
        const auto search = ui.TextInput(
            {.id = "log.search", .label = "Search", .hint = "Search logs..."}, search_);
        if (search.changed)
        {
            search_ = search.value;
            selected_log_index_ = -1;
            jump_to_latest_ = true;
        }

        const std::string_view query{search_};
        const LogLevelFilterOption &selected_filter = FindFilter(level_filter_);
        std::vector<const program::LogEntry *> filtered_logs;
        filtered_logs.reserve(logs.size());
        for (const program::LogEntry &log : logs)
        {
            if ((!selected_filter.matches_all && selected_filter.level != log.level) ||
                !MatchesSearch(log, query))
                continue;
            filtered_logs.push_back(&log);
        }

        const std::string copy_label = "Copy filtered (" +
                                       std::to_string(filtered_logs.size()) + ")";
        ui.SameLine();
        if (ui.Button({.id = "log.copy_filtered", .label = copy_label}).activated)
        {
            ui.SetClipboardText(FormatLogs(filtered_logs, 0, filtered_logs.size()));
        }
        const bool has_selection = selected_log_index_ >= 0 &&
                                   selected_log_index_ < static_cast<int>(filtered_logs.size());
        ui.SameLine();
        if (ui.Button({.id = "log.copy_selected", .label = "Copy selected",
                       .enabled = has_selection}).activated)
        {
            ui.SetClipboardText(FormatLogs(filtered_logs,
                                           static_cast<std::size_t>(selected_log_index_),
                                           static_cast<std::size_t>(selected_log_index_) + 1));
        }

        if (filtered_logs.empty())
        {
            selected_log_index_ = -1;
            ui.Text(logs.empty() ? "No logs" : "No matching logs", ui::UiTextRole::Muted);
            last_log_count_ = logs.size();
            jump_to_latest_ = false;
            return;
        }

        constexpr float kRowHeight = 20.0F;
        const bool scroll_to_end = jump_to_latest_ || (follow_latest_ && logs.size() > last_log_count_);
        ui.VirtualList("log.entries", filtered_logs.size(), kRowHeight, scroll_to_end,
            [&](const ui::UiListRange range)
            {
                for (std::size_t index = range.first; index < range.last; ++index)
                {
                    const program::LogEntry &log = *filtered_logs[index];
                    const std::string formatted_log = program::Logger::FetchStringFromLog(log);
                    const std::string id = "log.entry." + std::to_string(index);
                    const bool selected = selected_log_index_ == static_cast<int>(index);
                    [[maybe_unused]] const auto theme = ui.Theme(LogRowTheme(
                        colors_[static_cast<std::size_t>(log.level)]));
                    const auto row = ui.Selectable(
                        {.id = id, .label = formatted_log, .height = kRowHeight,
                         .span_row = true}, selected);
                    if (row.activated)
                        selected_log_index_ = static_cast<int>(index);
                }
            });

        jump_to_latest_ = false;
        last_log_count_ = logs.size();
    }

    std::string LogEditorView::FormatLogs(
        const std::vector<const program::LogEntry *> &logs,
        const std::size_t first, const std::size_t last)
    {
        std::string result;
        const std::size_t end = std::min(last, logs.size());
        for (std::size_t index = first; index < end; ++index)
        {
            result += program::Logger::FetchStringFromLog(*logs[index]);
            result.push_back('\n');
        }
        return result;
    }
}
