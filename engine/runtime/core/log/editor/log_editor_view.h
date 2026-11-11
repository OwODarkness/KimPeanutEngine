#ifndef KPENGINE_RUNTIME_LOG_EDITOR_VIEW_H
#define KPENGINE_RUNTIME_LOG_EDITOR_VIEW_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace kpengine
{
    class LogSystem;
    namespace program { struct LogEntry; }

    namespace editor
    {
        namespace ui { class UiContext; }
        struct LogEditorColor
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            float a = 1.0f;
        };

        using LogEditorColorTable = std::array<LogEditorColor, 5>;

        class LogEditorView final
        {
        public:
            LogEditorView();
            explicit LogEditorView(LogEditorColorTable colors);
            void RenderContent(ui::UiContext &ui, LogSystem *log_system);

        private:
            static std::string FormatLogs(
                const std::vector<const program::LogEntry *> &logs,
                std::size_t first, std::size_t last);

            LogEditorColorTable colors_;
            std::string search_;
            std::string level_filter_ = "all";
            bool follow_latest_ = true;
            bool jump_to_latest_ = false;
            std::size_t last_log_count_ = 0;
            int selected_log_index_ = -1;
        };
    }
}

#endif
