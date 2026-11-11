#ifndef KPENGINE_RUNTIME_SETTINGS_I_SETTINGS_EDITOR_ACCESS_H
#define KPENGINE_RUNTIME_SETTINGS_I_SETTINGS_EDITOR_ACCESS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "settings_store.h"

namespace kpengine::settings
{
    struct SettingsEditorSection
    {
        std::string key;
        std::string label;
        std::string category;
        std::uint32_t schema_version = 1;
        std::vector<SettingPropertyDefinition> properties;
        std::vector<SettingSaveScope> allowed_save_scopes;
        SettingApplyPolicy apply_policy = SettingApplyPolicy::Live;
    };

    struct SettingsEditorSnapshot
    {
        std::uint64_t revision = 0;
        std::vector<SettingsEditorSection> sections;
        SettingsResolvedSnapshot resolved;
        SettingsStoreSnapshot project_store;
        SettingsStoreSnapshot user_store;
        std::vector<SettingsResolutionDiagnostic> diagnostics;
        std::map<std::string, bool, std::less<>> provider_available;
    };

    enum class SettingsSaveRequestStatus : std::uint8_t
    {
        Pending,
        Succeeded,
        Failed,
        Cancelled,
    };

    struct SettingsSaveRequestResult
    {
        std::uint64_t request_id = 0;
        SettingSaveScope scope = SettingSaveScope::User;
        SettingsSaveRequestStatus status = SettingsSaveRequestStatus::Failed;
        std::string diagnostic;
    };

    struct SettingsSaveRequest
    {
        std::uint64_t request_id = 0;
        SettingSaveScope scope = SettingSaveScope::User;
        std::string section_key;
    };

    class ISettingsEditorAccess
    {
    public:
        virtual ~ISettingsEditorAccess() = default;
        virtual std::uint64_t GetRevision() const = 0;
        virtual SettingsEditorSnapshot CaptureSnapshot() const = 0;
        virtual SettingsEditResult SubmitEdit(std::string_view section_key,
                                             std::uint64_t base_revision,
                                             SettingValues changed_values) = 0;
        virtual std::optional<SettingsEditResult> GetEditResult(
            std::uint64_t request_id) = 0;
        virtual SettingsSaveRequestResult SubmitSave(SettingSaveScope scope,
                                                     std::string_view section_key) = 0;
        virtual std::optional<SettingsSaveRequestResult> GetSaveResult(
            std::uint64_t request_id) const = 0;
    };
}

#endif
