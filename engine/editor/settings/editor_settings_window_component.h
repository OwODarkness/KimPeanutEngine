#ifndef KPENGINE_EDITOR_SETTINGS_EDITOR_SETTINGS_WINDOW_COMPONENT_H
#define KPENGINE_EDITOR_SETTINGS_EDITOR_SETTINGS_WINDOW_COMPONENT_H

#include "editor/ui/component/editor_window_component.h"
#include "settings_editor_view.h"

namespace kpengine::editor
{
    class SettingsEditorWindowComponent final : public EditorWindowComponent
    {
    public:
        SettingsEditorWindowComponent(SettingsEditorModel &model,
                                      settings::ISettingsEditorAccess *access);

        void Open();
        bool HasCloseButton() const override { return true; }
        EditorPanelCloseDecision OnCloseRequested() override;
        EditorPanelCloseDecision PollCloseRequest() override;

    protected:
        void UpdateContent(bool visible) override;
        void RenderContent() override;
        void RenderContent(ui::UiContext &context) override;

    private:
        SettingsEditorModel &model_;
        EditorWindowVisibility visibility_{false};
        SettingsEditorView view_;
    };
}

#endif
