#ifndef KPENGINE_EDITOR_DOCKED_WORKSPACE_H
#define KPENGINE_EDITOR_DOCKED_WORKSPACE_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "editor_layout_model.h"

namespace kpengine::editor
{
    namespace ui { class UiContext; }
    class EditorMetric;

    class EditorDockedWorkspace final
    {
    public:
        explicit EditorDockedWorkspace(std::vector<std::unique_ptr<EditorMetric>> metrics);
        ~EditorDockedWorkspace();

        void AddPanel(std::string id, std::string title, EditorLayoutSlot slot,
                      std::function<void(ui::UiContext &)> draw);
        void ShowPanel(std::string_view id);
        void Render(ui::UiContext &context);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
