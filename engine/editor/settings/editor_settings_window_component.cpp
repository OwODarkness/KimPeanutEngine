#include "editor/settings/editor_settings_window_component.h"
#include "editor_ui/ui_backend.h"

#include <optional>

namespace kpengine::editor
{
    SettingsEditorWindowComponent::SettingsEditorWindowComponent(
        SettingsEditorModel &model, settings::ISettingsEditorAccess *access)
        : EditorWindowComponent("Settings",
                                EditorWindowConfig{.pos_x_ratio = 0.16f,
                                                   .pos_y_ratio = 0.12f,
                                                   .width_ratio = 0.68f,
                                                   .height_ratio = 0.76f,
                                                   .locked = false}),
          model_(model), view_(model, access)
    {
        SetVisibility(&visibility_);
    }

    void SettingsEditorWindowComponent::Open()
    {
        visibility_.SetOpen(true);
        view_.Open();
    }

    void SettingsEditorWindowComponent::UpdateContent(const bool visible)
    {
        (void)visible;
        view_.BeforeWindowRender();
    }

    EditorPanelCloseDecision SettingsEditorWindowComponent::OnCloseRequested()
    {
        return view_.BeginCloseRequest(model_.HasDrafts(), model_.HasPending())
                   ? EditorPanelCloseDecision::Accept
                   : EditorPanelCloseDecision::Defer;
    }

    EditorPanelCloseDecision SettingsEditorWindowComponent::PollCloseRequest()
    {
        const std::optional<bool> decision =
            view_.PollCloseRequest(model_.HasDrafts(), model_.HasPending());
        if (!decision.has_value())
        {
            return EditorPanelCloseDecision::Defer;
        }
        return *decision ? EditorPanelCloseDecision::Accept
                         : EditorPanelCloseDecision::Cancel;
    }

    void SettingsEditorWindowComponent::RenderContent()
    {
        view_.RenderContent();
    }

    void SettingsEditorWindowComponent::RenderContent(ui::UiContext &context)
    {
        context.SetHostContainer({.id = 0x53455454494e4753ULL,
                                  .role = ui::UiRole::Group,
                                  .label = "Settings"});
        view_.RenderContent(context);
    }
}
