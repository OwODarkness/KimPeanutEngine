#include "settings_editor_view.h"
#include "settings_property_fields_view.h"
#include "editor_ui/ui_backend.h"

#include "imgui.h"

#include <algorithm>
#include <optional>
#include <string>

namespace kpengine::editor
{
    namespace
    {
        constexpr float kNavigationMinWidth = 180.0f;
        constexpr float kNavigationMaxWidth = 300.0f;
        constexpr float kWideLayoutMinWidth = 680.0f;

        const settings::ResolvedSettingsSection *FindResolved(
            const settings::SettingsEditorSnapshot &snapshot, std::string_view key)
        {
            const auto found = std::find_if(
                snapshot.resolved.sections.begin(), snapshot.resolved.sections.end(),
                [key](const settings::ResolvedSettingsSection &section)
                {
                    return section.key == key;
                });
            return found == snapshot.resolved.sections.end() ? nullptr : &*found;
        }

        const char *SourceLabel(settings::SettingValueSource source)
        {
            switch (source)
            {
            case settings::SettingValueSource::CompiledDefault:
                return "Default";
            case settings::SettingValueSource::Project:
                return "Project";
            case settings::SettingValueSource::User:
                return "User";
            case settings::SettingValueSource::Session:
                return "Session";
            }
            return "Unknown";
        }

        const char *EditStateLabel(EditorSettingsEditState state)
        {
            switch (state)
            {
            case EditorSettingsEditState::Clean:
                return "Up to date";
            case EditorSettingsEditState::Dirty:
                return "Draft changes";
            case EditorSettingsEditState::Pending:
                return "Applying at next safe frame…";
            case EditorSettingsEditState::Applied:
                return "Applied";
            case EditorSettingsEditState::RestartRequired:
                return "Accepted for next launch";
            case EditorSettingsEditState::Rejected:
                return "Apply failed";
            case EditorSettingsEditState::Conflict:
                return "Settings changed elsewhere";
            case EditorSettingsEditState::Unavailable:
                return "Live settings unavailable";
            }
            return "Unknown state";
        }

        bool ScopeAllowed(const settings::SettingsEditorSection &section,
                          settings::SettingSaveScope scope)
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
                {
                    return true;
                }
            }
            return false;
        }

        const settings::ResolvedSettingValue *FindResolvedProperty(
            const settings::SettingsEditorSnapshot &snapshot,
            std::string_view section_key, std::string_view property_key)
        {
            const auto *const section = FindResolved(snapshot, section_key);
            if (section == nullptr)
            {
                return nullptr;
            }
            const auto property = section->properties.find(property_key);
            return property == section->properties.end() ? nullptr : &property->second;
        }

    }

    SettingsEditorView::SettingsEditorView(
        SettingsEditorModel &model, settings::ISettingsEditorAccess *access)
        : model_(model), access_(access)
    {
    }

    void SettingsEditorView::Open()
    {
        focus_requested_ = true;
        search_focus_requested_ = true;
    }

    void SettingsEditorView::BeforeWindowRender()
    {
        if (access_ != nullptr)
        {
            model_.Refresh(*access_);
        }
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
        else if (close_after_apply_ && close_retry_pending_ && !has_pending)
        {
            close_after_apply_ = false;
            close_retry_pending_ = false;
            close_confirmation_requested_ = has_drafts;
        }
        return std::nullopt;
    }

    void SettingsEditorView::RenderContent()
    {
        RenderContentImpl(nullptr);
    }

    void SettingsEditorView::RenderContent(ui::UiContext &context)
    {
        RenderContentImpl(&context);
    }

    void SettingsEditorView::RenderContentImpl(ui::UiContext *context)
    {
        if (focus_requested_)
        {
            ImGui::SetWindowFocus();
            focus_requested_ = false;
        }
        if (access_ == nullptr)
        {
            ImGui::TextWrapped("Settings are unavailable because Runtime did not publish the settings interface.");
            return;
        }

        const settings::SettingsEditorSnapshot &snapshot = model_.Snapshot();
        RenderCloseConfirmation();
        ImGui::TextDisabled("Changes are drafts until you apply them. Saving is a separate action.");
        char search_buffer[256]{};
        const std::string &search = model_.Search();
        const std::size_t copy_size = std::min(search.size(), sizeof(search_buffer) - 1);
        std::copy_n(search.data(), copy_size, search_buffer);
        ImGui::SetNextItemWidth(-1.0f);
        if (search_focus_requested_)
        {
            ImGui::SetKeyboardFocusHere();
            search_focus_requested_ = false;
        }
        if (ImGui::InputTextWithHint("##settings-search", "Search settings…", search_buffer,
                                     sizeof(search_buffer)))
        {
            model_.SetSearch(search_buffer);
        }
        if (!model_.Search().empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                model_.SetSearch({});
            }
        }

        std::size_t matching_sections = 0;
        for (const settings::SettingsEditorSection &section : snapshot.sections)
        {
            matching_sections += model_.MatchesSearch(section) ? 1u : 0u;
        }
        if (snapshot.sections.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("No settings sections are registered.");
            return;
        }
        if (matching_sections == 0)
        {
            ImGui::Spacing();
            ImGui::TextWrapped("No settings match this search.");
            if (ImGui::Button("Clear search"))
            {
                model_.SetSearch({});
            }
            return;
        }

        const float width = ImGui::GetContentRegionAvail().x;
        if (width < kWideLayoutMinWidth)
        {
            RenderNavigation(snapshot, width);
            ImGui::Separator();
            const auto selected = std::find_if(
                snapshot.sections.begin(), snapshot.sections.end(),
                [this](const settings::SettingsEditorSection &section)
                {
                    return section.key == model_.SelectedSection();
                });
            if (selected != snapshot.sections.end() && model_.MatchesSearch(*selected))
            {
                RenderSection(snapshot, *selected, context);
            }
            else
            {
                ImGui::TextWrapped("Choose a matching section from the navigation list.");
            }
        }
        else
        {
            const float navigation_width = std::clamp(width * 0.28f,
                                                      kNavigationMinWidth,
                                                      kNavigationMaxWidth);
            if (ImGui::BeginTable("##settings-panes", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                                  ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 4.5f)))
            {
                ImGui::TableSetupColumn("##navigation", ImGuiTableColumnFlags_WidthFixed,
                                        navigation_width);
                ImGui::TableSetupColumn("##properties", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                RenderNavigation(snapshot, navigation_width);
                ImGui::TableSetColumnIndex(1);
                const auto selected = std::find_if(
                    snapshot.sections.begin(), snapshot.sections.end(),
                    [this](const settings::SettingsEditorSection &section)
                    {
                        return section.key == model_.SelectedSection();
                    });
                if (selected != snapshot.sections.end() &&
                    model_.MatchesSearch(*selected))
                {
                    RenderSection(snapshot, *selected, context);
                }
                else
                {
                    ImGui::TextWrapped("Choose a matching section from the navigation list.");
                }
                ImGui::EndTable();
            }
        }
    }

    void SettingsEditorView::RenderNavigation(
        const settings::SettingsEditorSnapshot &snapshot, float width)
    {
        const float height = ImGui::GetContentRegionAvail().y;
        if (width < kWideLayoutMinWidth)
        {
            const settings::SettingsEditorSection *selected = nullptr;
            for (const settings::SettingsEditorSection &section : snapshot.sections)
            {
                if (section.key == model_.SelectedSection())
                {
                    selected = &section;
                    break;
                }
            }
            const std::string preview = selected != nullptr
                                            ? selected->category + " / " + selected->label
                                            : "Select a section";
            if (ImGui::BeginCombo("Section", preview.c_str()))
            {
                for (const settings::SettingsEditorSection &section : snapshot.sections)
                {
                    if (!model_.MatchesSearch(section))
                    {
                        continue;
                    }
                    const std::string label = section.category + " / " + section.label;
                    if (ImGui::Selectable(label.c_str(), section.key == model_.SelectedSection()))
                    {
                        model_.SelectSection(section.key);
                    }
                }
                ImGui::EndCombo();
            }
            return;
        }

        if (!ImGui::BeginChild("##settings-navigation", ImVec2(0.0f, height), true))
        {
            ImGui::EndChild();
            return;
        }
        std::string previous_category;
        for (const settings::SettingsEditorSection &section : snapshot.sections)
        {
            if (!model_.MatchesSearch(section))
            {
                continue;
            }
            if (section.category != previous_category)
            {
                if (!previous_category.empty())
                {
                    ImGui::Spacing();
                }
                ImGui::TextDisabled("%s", section.category.c_str());
                previous_category = section.category;
            }
            ImGui::PushID(section.key.c_str());
            if (ImGui::Selectable(section.label.c_str(),
                                  section.key == model_.SelectedSection()))
            {
                model_.SelectSection(section.key);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void SettingsEditorView::RenderSection(
        const settings::SettingsEditorSnapshot &snapshot,
        const settings::SettingsEditorSection &section, ui::UiContext *context)
    {
        EditorSettingsDraft *const draft = model_.Draft(section.key);
        if (draft == nullptr)
        {
            ImGui::TextDisabled("Section values are unavailable.");
            return;
        }
        ImGui::Text("%s", section.label.c_str());
        ImGui::SameLine();
        if (section.apply_policy == settings::SettingApplyPolicy::RestartRequired)
        {
            ImGui::TextDisabled("Restart required");
        }
        else
        {
            const auto provider = snapshot.provider_available.find(section.key);
            const bool available = provider != snapshot.provider_available.end() &&
                                   provider->second;
            ImGui::TextDisabled(available ? "Live" : "Live provider unavailable");
        }
        RenderStatus(draft, section, snapshot);
        ImGui::Separator();

        if (section.apply_policy == settings::SettingApplyPolicy::RestartRequired)
        {
            ImGui::TextWrapped("Accepted changes update next-launch settings. The current device is unchanged until restart.");
        }
        else
        {
            const auto provider = snapshot.provider_available.find(section.key);
            if (provider == snapshot.provider_available.end() || !provider->second)
            {
                ImGui::TextWrapped("This section is configured but has no live provider in the current host.");
            }
        }

        ImGui::BeginDisabled(draft->pending_request.has_value());
        const auto render_property_value = [this, draft, context, section_key = section.key](
                                                const settings::SettingPropertyDefinition &property,
                                                bool show_label)
        {
            const auto value = draft->values.find(property.key);
            if (value == draft->values.end())
            {
                ImGui::TextDisabled("Unavailable: value is missing");
                return;
            }
            reflection::ReflectionValue updated = value->second;
            bool changed = false;
            if (context != nullptr)
            {
                const ui::UiEditResult edit = SettingsPropertyFieldsView::Draw(
                    *context, section_key, property, value->second,
                    !draft->pending_request.has_value());
                changed = edit.changed;
                if (changed)
                {
                    std::visit([&updated](const auto &candidate) { updated = candidate; }, edit.value);
                }
            }
            else
            {
                ImGui::TextDisabled("Neutral UI context unavailable");
            }
            if (changed)
            {
                (void)model_.SetValue(property.key, updated);
            }
            if (property.value_type == reflection::ReflectionValueType::String)
            {
                ImGui::TextDisabled("Unavailable: text settings are not supported yet");
            }
        };
        const float property_width = ImGui::GetContentRegionAvail().x;
        if (property_width < 520.0f)
        {
            for (const settings::SettingPropertyDefinition &property : section.properties)
            {
                ImGui::TextUnformatted(property.metadata.display_name.empty()
                                           ? property.key.c_str()
                                           : property.metadata.display_name.c_str());
                if (!property.metadata.tooltip.empty() && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", property.metadata.tooltip.c_str());
                }
                render_property_value(property, false);
                const settings::ResolvedSettingValue *const resolved =
                    FindResolvedProperty(snapshot, section.key, property.key);
                ImGui::TextDisabled("%s", resolved != nullptr
                                                ? SourceLabel(resolved->source)
                                                : "Unknown");
                ImGui::Separator();
            }
        }
        else if (ImGui::BeginTable("##settings-properties", 3,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                       ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 78.0f);
            ImGui::TableHeadersRow();
            for (const settings::SettingPropertyDefinition &property : section.properties)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(property.metadata.display_name.empty()
                                           ? property.key.c_str()
                                           : property.metadata.display_name.c_str());
                if (!property.metadata.tooltip.empty() && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", property.metadata.tooltip.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                render_property_value(property, false);
                ImGui::TableSetColumnIndex(2);
                const settings::ResolvedSettingValue *const resolved =
                    FindResolvedProperty(snapshot, section.key, property.key);
                ImGui::TextDisabled("%s", resolved != nullptr
                                                ? SourceLabel(resolved->source)
                                                : "Unknown");
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        const bool pending = draft->pending_request.has_value();
        const bool provider_missing = section.apply_policy == settings::SettingApplyPolicy::Live &&
            (!snapshot.provider_available.contains(section.key) ||
             !snapshot.provider_available.at(section.key));
        ImGui::BeginDisabled(pending || provider_missing ||
                             draft->state == EditorSettingsEditState::Conflict ||
                             !DraftHasChanges(*draft));
        if (ImGui::Button("Apply"))
        {
            (void)model_.Apply(*access_, section.key);
        }
        ImGui::EndDisabled();
        if (draft->state == EditorSettingsEditState::Conflict)
        {
            ImGui::SameLine();
            if (ImGui::Button("Rebase draft"))
            {
                (void)model_.Rebase(section.key);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Review the updated values before applying.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(pending || !DraftHasChanges(*draft));
        if (ImGui::Button("Revert"))
        {
            model_.Revert(section.key);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(pending);
        if (ImGui::Button("Restore defaults"))
        {
            model_.RestoreDefaults(section.key);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool can_save_project = ScopeAllowed(section, settings::SettingSaveScope::Project);
        ImGui::BeginDisabled(!can_save_project || pending ||
                             model_.LastSaveResult().has_value() &&
                                 model_.LastSaveResult()->status ==
                                     settings::SettingsSaveRequestStatus::Pending);
        if (ImGui::Button("Save to project"))
        {
            save_scope_ = settings::SettingSaveScope::Project;
            save_section_ = section.key;
            (void)model_.Save(*access_, settings::SettingSaveScope::Project, section.key);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool can_save_user = ScopeAllowed(section, settings::SettingSaveScope::User);
        ImGui::BeginDisabled(!can_save_user || pending ||
                             model_.LastSaveResult().has_value() &&
                                 model_.LastSaveResult()->status ==
                                     settings::SettingsSaveRequestStatus::Pending);
        if (ImGui::Button("Save to user"))
        {
            save_scope_ = settings::SettingSaveScope::User;
            save_section_ = section.key;
            (void)model_.Save(*access_, settings::SettingSaveScope::User, section.key);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Save writes accepted values; unsubmitted drafts are not included.");
    }

    void SettingsEditorView::RenderStatus(
        const EditorSettingsDraft *draft,
        const settings::SettingsEditorSection &section,
        const settings::SettingsEditorSnapshot &snapshot)
    {
        if (draft != nullptr)
        {
            ImGui::Text("%s", EditStateLabel(draft->state));
            if (!draft->diagnostic.empty())
            {
                ImGui::TextWrapped("%s", draft->diagnostic.c_str());
            }
        }
        if (save_section_ == section.key && model_.LastSaveResult().has_value())
        {
            const settings::SettingsSaveRequestResult &save = *model_.LastSaveResult();
            if (save.status != settings::SettingsSaveRequestStatus::Pending ||
                save_scope_.has_value())
            {
                ImGui::TextWrapped("%s: %s",
                                   save.status == settings::SettingsSaveRequestStatus::Succeeded
                                       ? "Saved"
                                       : save.status == settings::SettingsSaveRequestStatus::Pending
                                             ? "Saving"
                                             : "Save failed",
                                   save.diagnostic.c_str());
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
                {
                    ImGui::TextWrapped("Save unavailable: %s", diagnostic.message.c_str());
                }
            }
        }
    }

    void SettingsEditorView::RenderCloseConfirmation()
    {
        if (close_confirmation_requested_)
        {
            ImGui::OpenPopup("Unsaved settings");
            close_confirmation_requested_ = false;
        }
        if (ImGui::BeginPopupModal("Unsaved settings", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("This window has unsubmitted settings drafts. Conflicted drafts must be rebased or discarded before they can be applied.");
            ImGui::Spacing();
            if (ImGui::Button("Apply all drafts"))
            {
                (void)model_.ApplyAll(*access_);
                close_after_apply_ = true;
                close_retry_pending_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard all"))
            {
                for (const settings::SettingsEditorSection &section : model_.Snapshot().sections)
                {
                    model_.Revert(section.key);
                }
                close_requested_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                close_cancelled_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
