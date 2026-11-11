#ifndef KPENGINE_EDITOR_SETTINGS_EDITOR_SETTINGS_MODEL_H
#define KPENGINE_EDITOR_SETTINGS_EDITOR_SETTINGS_MODEL_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "settings/i_settings_editor_access.h"

namespace kpengine::editor
{
    enum class EditorSettingsEditState : std::uint8_t
    {
        Clean,
        Dirty,
        Pending,
        Applied,
        RestartRequired,
        Rejected,
        Conflict,
        Unavailable,
    };

    struct EditorSettingsDraft
    {
        std::uint64_t base_revision = 0;
        settings::SettingValues base_values;
        settings::SettingValues values;
        std::optional<std::uint64_t> pending_request;
        EditorSettingsEditState state = EditorSettingsEditState::Clean;
        std::string diagnostic;
    };

    class SettingsEditorModel final
    {
    public:
        void Refresh(settings::ISettingsEditorAccess &access);
        void SelectSection(std::string_view section_key);
        void SetSearch(std::string search);
        bool MatchesSearch(const settings::SettingsEditorSection &section) const;
        bool SetValue(std::string_view property_key,
                      const reflection::ReflectionValue &value);
        void Revert(std::string_view section_key);
        void RestoreDefaults(std::string_view section_key);
        settings::SettingsEditResult Apply(settings::ISettingsEditorAccess &access,
                                           std::string_view section_key);
        std::size_t ApplyAll(settings::ISettingsEditorAccess &access);
        bool Rebase(std::string_view section_key);
        settings::SettingsSaveRequestResult Save(settings::ISettingsEditorAccess &access,
                                                 settings::SettingSaveScope scope,
                                                 std::string_view section_key);
        void Poll(settings::ISettingsEditorAccess &access);

        const settings::SettingsEditorSnapshot &Snapshot() const noexcept { return snapshot_; }
        const std::string &SelectedSection() const noexcept { return selected_section_; }
        const std::string &Search() const noexcept { return search_; }
        const EditorSettingsDraft *Draft(std::string_view section_key) const noexcept;
        EditorSettingsDraft *Draft(std::string_view section_key) noexcept;
        const std::optional<settings::SettingsSaveRequestResult> &LastSaveResult() const noexcept
        {
            return last_save_result_;
        }
        bool HasDrafts() const noexcept;
        bool HasPending() const noexcept;

    private:
        const settings::SettingsEditorSection *FindSchema(std::string_view section_key) const;
        const settings::ResolvedSettingsSection *FindResolved(
            std::string_view section_key) const;
        void InitializeDraft(const settings::SettingsEditorSection &schema,
                             const settings::ResolvedSettingsSection *resolved,
                             EditorSettingsDraft &draft) const;
        void RefreshCleanDrafts();

        settings::SettingsEditorSnapshot snapshot_;
        std::map<std::string, EditorSettingsDraft, std::less<>> drafts_;
        std::string selected_section_;
        std::string search_;
        std::optional<std::uint64_t> pending_save_request_;
        std::optional<settings::SettingsSaveRequestResult> last_save_result_;
    };
}

#endif
