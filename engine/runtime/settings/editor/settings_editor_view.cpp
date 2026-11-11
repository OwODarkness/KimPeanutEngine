#include "settings_editor_view.h"
#include "settings_property_fields_view.h"
#include "editor_ui/ui_backend.h"

#include <algorithm>
#include <string>
#include <vector>

namespace kpengine::editor
{
    namespace
    {
        const settings::ResolvedSettingsSection *FindResolved(
            const settings::SettingsEditorSnapshot &snapshot, const std::string_view key)
        {
            const auto found = std::find_if(
                snapshot.resolved.sections.begin(), snapshot.resolved.sections.end(),
                [key](const settings::ResolvedSettingsSection &section)
                {
                    return section.key == key;
                });
            return found == snapshot.resolved.sections.end() ? nullptr : &*found;
        }

        const char *SourceLabel(const settings::SettingValueSource source)
        {
            switch (source)
            {
            case settings::SettingValueSource::CompiledDefault: return "Default";
            case settings::SettingValueSource::Project: return "Project";
            case settings::SettingValueSource::User: return "User";
            case settings::SettingValueSource::Session: return "Session";
            }
            return "Unknown";
        }

        const char *EditStateLabel(const EditorSettingsEditState state)
        {
            switch (state)
            {
            case EditorSettingsEditState::Clean: return "Up to date";
            case EditorSettingsEditState::Dirty: return "Draft changes";
            case EditorSettingsEditState::Pending: return "Applying at next safe frame…";
            case EditorSettingsEditState::Applied: return "Applied";
            case EditorSettingsEditState::RestartRequired: return "Accepted for next launch";
            case EditorSettingsEditState::Rejected: return "Apply failed";
            case EditorSettingsEditState::Conflict: return "Settings changed elsewhere";
            case EditorSettingsEditState::Unavailable: return "Live settings unavailable";
            }
            return "Unknown state";
        }

        bool ScopeAllowed(const settings::SettingsEditorSection &section,
                          const settings::SettingSaveScope scope)
        {
            return std::find(section.allowed_save_scopes.begin(),
                             section.allowed_save_scopes.end(), scope) !=
                   section.allowed_save_scopes.end();
        }

        bool DraftHasChanges(const EditorSettingsDraft &draft)
        {
            for (const auto &[key, value] : draft.values)
            {
                const auto base = draft.base_values.find(key);
                if (base == draft.base_values.end() || base->second != value)
                    return true;
            }
            return false;
        }

        const settings::ResolvedSettingValue *FindResolvedProperty(
            const settings::SettingsEditorSnapshot &snapshot,
            const std::string_view section_key, const std::string_view property_key)
        {
            const auto *const section = FindResolved(snapshot, section_key);
            if (section == nullptr)
                return nullptr;
            const auto property = section->properties.find(property_key);
            return property == section->properties.end() ? nullptr : &property->second;
        }

        bool SavePending(const SettingsEditorModel &model)
        {
            return model.LastSaveResult().has_value() &&
                   model.LastSaveResult()->status ==
                       settings::SettingsSaveRequestStatus::Pending;
        }
    }

    SettingsEditorView::SettingsEditorView(
        SettingsEditorModel &model, settings::ISettingsEditorAccess *access)
        : model_(model), access_(access)
    {
    }

    void SettingsEditorView::BeforeWindowRender()
    {
        if (access_ != nullptr)
            model_.Refresh(*access_);
    }

    bool SettingsEditorView::BeginCloseRequest(const bool has_drafts,
                                               const bool has_pending)
    {
        close_cancelled_ = false;
        if (has_pending)
        {
            close_after_apply_ = true;
            close_retry_pending_ = true;
            return false;
        }
        if (has_drafts)
        {
            close_confirmation_requested_ = true;
            return false;
        }
        return true;
    }

    std::optional<bool> SettingsEditorView::PollCloseRequest(const bool has_drafts,
                                                              const bool has_pending)
    {
        if (close_requested_)
        {
            close_requested_ = false;
            return true;
        }
        if (close_cancelled_)
        {
            close_cancelled_ = false;
            return false;
        }
        if (close_after_apply_ && !has_drafts && !has_pending)
        {
            close_after_apply_ = false;
            close_retry_pending_ = false;
            return true;
        }
        if (close_after_apply_ && close_retry_pending_ && !has_pending)
        {
            close_after_apply_ = false;
            close_retry_pending_ = false;
            close_confirmation_requested_ = has_drafts;
        }
        return std::nullopt;
    }

    void SettingsEditorView::RenderContent(ui::UiContext &context)
    {
        if (access_ == nullptr)
        {
            context.WrappedText(
                "Settings are unavailable because Runtime did not publish the settings interface.");
            return;
        }

        const settings::SettingsEditorSnapshot &snapshot = model_.Snapshot();
        RenderCloseConfirmation(context);
        context.Text("Changes are drafts until you apply them. Saving is a separate action.",
                     ui::UiTextRole::Muted);
        RenderNavigation(context, snapshot);

        std::size_t matching_sections = 0;
        for (const settings::SettingsEditorSection &section : snapshot.sections)
            matching_sections += model_.MatchesSearch(section) ? 1u : 0u;

        if (snapshot.sections.empty())
        {
            context.WrappedText("No settings sections are registered.");
            return;
        }
        if (matching_sections == 0)
        {
            context.WrappedText("No settings match this search.");
            if (context.Button({.id = "settings.search.clear_empty", .label = "Clear search"})
                    .activated)
                model_.SetSearch({});
            return;
        }

        const auto selected = std::find_if(
            snapshot.sections.begin(), snapshot.sections.end(),
            [this](const settings::SettingsEditorSection &section)
            {
                return section.key == model_.SelectedSection();
            });
        if (selected != snapshot.sections.end() && model_.MatchesSearch(*selected))
            RenderSection(context, snapshot, *selected);
        else
            context.WrappedText("Choose a matching section from the navigation list.");
    }

    void SettingsEditorView::RenderNavigation(
        ui::UiContext &context, const settings::SettingsEditorSnapshot &snapshot)
    {
        const auto search = context.TextInput(
            {.id = "settings.search", .label = "Search", .hint = "Search settings…"},
            model_.Search());
        if (search.changed)
            model_.SetSearch(search.value);
        if (!model_.Search().empty())
        {
            context.SameLine();
            if (context.Button({.id = "settings.search.clear", .label = "Clear"}).activated)
                model_.SetSearch({});
        }

        std::vector<ui::UiChoice> choices;
        for (const settings::SettingsEditorSection &section : snapshot.sections)
        {
            if (!model_.MatchesSearch(section))
                continue;
            choices.push_back({section.key, section.category + " / " + section.label,
                               ui::UiValue{section.key}});
        }
        if (choices.empty())
            return;

        const bool selection_visible = std::any_of(
            choices.begin(), choices.end(), [this](const ui::UiChoice &choice)
            {
                return choice.key == model_.SelectedSection();
            });
        if (!selection_visible)
            model_.SelectSection(choices.front().key);
        const std::string selected = model_.SelectedSection();
        const auto choice = context.Combo<std::string>(
            {.id = "settings.section", .label = "Section"}, selected, std::move(choices));
        if (choice.changed)
            model_.SelectSection(choice.value);
    }

    void SettingsEditorView::RenderSection(
        ui::UiContext &context, const settings::SettingsEditorSnapshot &snapshot,
        const settings::SettingsEditorSection &section)
    {
        EditorSettingsDraft *const draft = model_.Draft(section.key);
        if (draft == nullptr)
        {
            context.Text("Section values are unavailable.", ui::UiTextRole::Warning);
            return;
        }

        context.Section(section.label);
        if (section.apply_policy == settings::SettingApplyPolicy::RestartRequired)
        {
            context.Text("Restart required", ui::UiTextRole::Warning);
            context.WrappedText(
                "Accepted changes update next-launch settings. The current device is unchanged until restart.");
        }
        else
        {
            const auto provider = snapshot.provider_available.find(section.key);
            const bool available = provider != snapshot.provider_available.end() &&
                                   provider->second;
            context.Text(available ? "Live" : "Live provider unavailable",
                         available ? ui::UiTextRole::Success : ui::UiTextRole::Warning);
            if (!available)
                context.WrappedText(
                    "This section is configured but has no live provider in the current host.");
        }

        RenderStatus(context, draft, section, snapshot);

        const auto properties = [&]
        {
            for (const settings::SettingPropertyDefinition &property : section.properties)
            {
                context.TableRow();
                context.TableColumn();
                context.WrappedText(property.metadata.display_name.empty()
                                        ? property.key
                                        : property.metadata.display_name);
                context.TableColumn();
                const auto value = draft->values.find(property.key);
                if (value == draft->values.end())
                {
                    context.Text("Unavailable: value is missing", ui::UiTextRole::Warning);
                }
                else
                {
                    const ui::UiEditResult edit = SettingsPropertyFieldsView::Draw(
                        context, section.key, property, value->second,
                        !draft->pending_request.has_value());
                    if (edit.changed)
                    {
                        reflection::ReflectionValue updated = value->second;
                        std::visit([&updated](const auto &candidate) { updated = candidate; },
                                   edit.value);
                        (void)model_.SetValue(property.key, updated);
                    }
                }
                context.TableColumn();
                const settings::ResolvedSettingValue *const resolved =
                    FindResolvedProperty(snapshot, section.key, property.key);
                context.Text(resolved != nullptr ? SourceLabel(resolved->source) : "Unknown",
                             ui::UiTextRole::Muted);
            }
        };

        const ui::UiVec2 available = context.AvailableContent();
        constexpr float reserved_height = 270.0F;
        const float table_height = std::max(120.0F, available.y - reserved_height);
        constexpr ui::UiTableColumn columns[] = {
            {"Setting", 0.0F}, {"Value", 0.0F}, {"Source", 86.0F}};
        context.Table("settings.properties", columns, table_height, true, properties);

        const bool pending = draft->pending_request.has_value();
        const bool provider_missing = section.apply_policy == settings::SettingApplyPolicy::Live &&
            (!snapshot.provider_available.contains(section.key) ||
             !snapshot.provider_available.at(section.key));
        const bool has_changes = DraftHasChanges(*draft);
        if (context.Button({.id = "settings.apply", .label = "Apply",
                            .enabled = !pending && !provider_missing && has_changes &&
                                       draft->state != EditorSettingsEditState::Conflict})
                .activated)
            (void)model_.Apply(*access_, section.key);

        if (draft->state == EditorSettingsEditState::Conflict)
        {
            context.SameLine();
            if (context.Button({.id = "settings.rebase", .label = "Rebase draft"}).activated)
                (void)model_.Rebase(section.key);
            context.Text("Review the updated values before applying.", ui::UiTextRole::Muted);
        }
        context.SameLine();
        if (context.Button({.id = "settings.revert", .label = "Revert",
                            .enabled = !pending && has_changes}).activated)
            model_.Revert(section.key);
        context.SameLine();
        if (context.Button({.id = "settings.defaults", .label = "Restore defaults",
                            .enabled = !pending}).activated)
            model_.RestoreDefaults(section.key);

        const bool save_pending = SavePending(model_);
        const bool can_save_project = ScopeAllowed(section, settings::SettingSaveScope::Project);
        context.SameLine();
        if (context.Button({.id = "settings.save.project", .label = "Save to project",
                            .enabled = can_save_project && !pending && !save_pending}).activated)
        {
            save_scope_ = settings::SettingSaveScope::Project;
            save_section_ = section.key;
            (void)model_.Save(*access_, settings::SettingSaveScope::Project, section.key);
        }
        const bool can_save_user = ScopeAllowed(section, settings::SettingSaveScope::User);
        context.SameLine();
        if (context.Button({.id = "settings.save.user", .label = "Save to user",
                            .enabled = can_save_user && !pending && !save_pending}).activated)
        {
            save_scope_ = settings::SettingSaveScope::User;
            save_section_ = section.key;
            (void)model_.Save(*access_, settings::SettingSaveScope::User, section.key);
        }
        context.Text("Save writes accepted values; unsubmitted drafts are not included.",
                     ui::UiTextRole::Muted);
    }

    void SettingsEditorView::RenderStatus(
        ui::UiContext &context, const EditorSettingsDraft *draft,
        const settings::SettingsEditorSection &section,
        const settings::SettingsEditorSnapshot &snapshot)
    {
        if (draft != nullptr)
        {
            const ui::UiTextRole role = draft->state == EditorSettingsEditState::Rejected
                                            ? ui::UiTextRole::Error
                                        : draft->state == EditorSettingsEditState::Conflict
                                            ? ui::UiTextRole::Warning
                                        : draft->state == EditorSettingsEditState::Applied
                                            ? ui::UiTextRole::Success
                                            : ui::UiTextRole::Muted;
            context.Text(EditStateLabel(draft->state), role);
            if (!draft->diagnostic.empty())
                context.WrappedText(draft->diagnostic);
        }
        if (save_section_ == section.key && model_.LastSaveResult().has_value())
        {
            const settings::SettingsSaveRequestResult &save = *model_.LastSaveResult();
            if (save.status != settings::SettingsSaveRequestStatus::Pending || save_scope_)
            {
                const char *label = save.status == settings::SettingsSaveRequestStatus::Succeeded
                                        ? "Saved"
                                    : save.status == settings::SettingsSaveRequestStatus::Pending
                                        ? "Saving"
                                        : "Save failed";
                context.WrappedText(std::string{label} + ": " + save.diagnostic);
            }
        }
        const settings::SettingsStoreSnapshot &store =
            save_scope_ == settings::SettingSaveScope::Project
                ? snapshot.project_store
                : snapshot.user_store;
        if (!store.writable)
        {
            for (const settings::SettingsStoreDiagnostic &diagnostic : store.diagnostics)
            {
                if (diagnostic.section_key.empty() || diagnostic.section_key == section.key)
                    context.WrappedText("Save unavailable: " + diagnostic.message);
            }
        }
    }

    void SettingsEditorView::RenderCloseConfirmation(ui::UiContext &context)
    {
        if (!close_confirmation_requested_)
            return;

        context.Section("Unsaved settings");
        context.WrappedText(
            "This window has unsubmitted settings drafts. Conflicted drafts must be rebased or discarded before they can be applied.");
        if (context.Button({.id = "settings.close.apply", .label = "Apply all drafts"}).activated)
        {
            (void)model_.ApplyAll(*access_);
            close_after_apply_ = true;
            close_retry_pending_ = true;
            close_confirmation_requested_ = false;
        }
        context.SameLine();
        if (context.Button({.id = "settings.close.discard", .label = "Discard all"}).activated)
        {
            for (const settings::SettingsEditorSection &section : model_.Snapshot().sections)
                model_.Revert(section.key);
            close_requested_ = true;
            close_confirmation_requested_ = false;
        }
        context.SameLine();
        if (context.Button({.id = "settings.close.cancel", .label = "Cancel"}).activated)
        {
            close_cancelled_ = true;
            close_confirmation_requested_ = false;
        }
    }
}
