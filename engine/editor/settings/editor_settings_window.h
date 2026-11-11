#ifndef KPENGINE_EDITOR_SETTINGS_EDITOR_SETTINGS_WINDOW_H
#define KPENGINE_EDITOR_SETTINGS_EDITOR_SETTINGS_WINDOW_H

#include "editor/ui/component/editor_window_visibility.h"
#include "settings_editor_view.h"

namespace kpengine::editor
{
    namespace ui { class UiContext; }
    class SettingsEditorWindow final
    {
    public:
        SettingsEditorWindow(SettingsEditorModel &model,
                                      settings::ISettingsEditorAccess *access);

        void Open();
        bool IsVisible() const noexcept { return visibility_.IsOpen(); }
        void Update(bool visible);
        void RenderWithContext(ui::UiContext &context);

    private:
        SettingsEditorModel &model_;
        EditorWindowVisibility visibility_{false};
        SettingsEditorView view_;
    };
}

#endif
