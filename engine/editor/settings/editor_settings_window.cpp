#include "editor/settings/editor_settings_window.h"
#include "editor_ui/ui_backend.h"

#include <optional>

namespace kpengine::editor
{
    namespace
    {
        constexpr float kWindowX = 0.16F;
        constexpr float kWindowY = 0.12F;
        constexpr float kWindowWidth = 0.68F;
        constexpr float kWindowHeight = 0.76F;
    }

    SettingsEditorWindow::SettingsEditorWindow(
        SettingsEditorModel &model, settings::ISettingsEditorAccess *access)
        : model_(model), view_(model, access)
    {
    }

    void SettingsEditorWindow::Open()
    {
        visibility_.SetOpen(true);
    }

    void SettingsEditorWindow::Update(const bool visible)
    {
        if (visible)
            view_.BeforeWindowRender();

        if (!visibility_.IsCloseDeferred())
            return;
        const std::optional<bool> decision =
            view_.PollCloseRequest(model_.HasDrafts(), model_.HasPending());
        if (decision.has_value())
            visibility_.ResolveDeferredClose(*decision);
    }

    void SettingsEditorWindow::RenderWithContext(ui::UiContext &context)
    {
        if (!visibility_.IsOpen())
            return;

        const ui::UiViewportInfo &viewport = context.Viewport();
        const ui::UiVec2 position{
            viewport.logical_origin.x + kWindowX * viewport.logical_extent.x,
            viewport.logical_origin.y + kWindowY * viewport.logical_extent.y};
        const ui::UiVec2 size{kWindowWidth * viewport.logical_extent.x,
                              kWindowHeight * viewport.logical_extent.y};
        bool open = visibility_.IsOpen();
        {
            const auto window = context.Window(
                {.id = "settings.window", .label = "Settings"}, open, position, size);
            if (window.ContentVisible())
                view_.RenderContent(context);
        }

        if (!open)
        {
            const EditorPanelCloseDecision decision =
                view_.BeginCloseRequest(model_.HasDrafts(), model_.HasPending())
                    ? EditorPanelCloseDecision::Accept
                    : EditorPanelCloseDecision::Defer;
            visibility_.RequestClose(decision);
        }
    }
}
