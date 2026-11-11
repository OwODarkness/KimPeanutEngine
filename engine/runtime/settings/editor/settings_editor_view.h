#ifndef KPENGINE_RUNTIME_SETTINGS_EDITOR_SETTINGS_EDITOR_VIEW_H
#define KPENGINE_RUNTIME_SETTINGS_EDITOR_SETTINGS_EDITOR_VIEW_H

#include <optional>
#include <string>

#include "editor_settings_model.h"

namespace kpengine::editor
{
    namespace ui { class UiContext; }
    class SettingsEditorView final
    {
    public:
        SettingsEditorView(SettingsEditorModel &model,
                           settings::ISettingsEditorAccess *access);

        void BeforeWindowRender();
        void RenderContent(ui::UiContext &context);
        bool BeginCloseRequest(bool has_drafts, bool has_pending);
        std::optional<bool> PollCloseRequest(bool has_drafts, bool has_pending);

    private:
        void RenderNavigation(ui::UiContext &context,
                              const settings::SettingsEditorSnapshot &snapshot);
        void RenderSection(ui::UiContext &context,
                           const settings::SettingsEditorSnapshot &snapshot,
                           const settings::SettingsEditorSection &section);
        void RenderCloseConfirmation(ui::UiContext &context);
        void RenderStatus(ui::UiContext &context, const EditorSettingsDraft *draft,
                          const settings::SettingsEditorSection &section,
                          const settings::SettingsEditorSnapshot &snapshot);

        SettingsEditorModel &model_;
        settings::ISettingsEditorAccess *access_ = nullptr;
        bool close_confirmation_requested_ = false;
        bool close_after_apply_ = false;
        bool close_retry_pending_ = false;
        bool close_requested_ = false;
        bool close_cancelled_ = false;
        std::optional<settings::SettingSaveScope> save_scope_;
        std::string save_section_;
    };
}

#endif
