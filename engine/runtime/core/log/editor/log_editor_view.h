#ifndef KPENGINE_RUNTIME_LOG_EDITOR_VIEW_H
#define KPENGINE_RUNTIME_LOG_EDITOR_VIEW_H

#include <array>
#include <cstddef>

namespace kpengine
{
    class LogSystem;

    namespace editor
    {
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
            explicit LogEditorView(LogEditorColorTable colors);
            void RenderContent(LogSystem *log_system);

        private:
            LogEditorColorTable colors_;
            std::array<char, 256> search_buffer_{};
            std::size_t level_filter_index_ = 0;
            bool follow_latest_ = true;
            bool jump_to_latest_ = false;
            std::size_t last_log_count_ = 0;
            int selection_anchor_index_ = -1;
            int selection_caret_index_ = -1;
        };
    }
}

#endif
