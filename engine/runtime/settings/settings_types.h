#ifndef KPENGINE_RUNTIME_SETTINGS_SETTINGS_TYPES_H
#define KPENGINE_RUNTIME_SETTINGS_SETTINGS_TYPES_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "reflection/reflection_types.h"

namespace kpengine::settings
{
    using SettingValues = std::map<std::string, reflection::ReflectionValue, std::less<>>;
    using SettingsLayerOverrides =
        std::map<std::string, SettingValues, std::less<>>;

    enum class SettingSaveScope : uint8_t
    {
        Project,
        User,
    };

    enum class SettingValueSource : uint8_t
    {
        CompiledDefault,
        Project,
        User,
        Session,
    };

    struct ResolvedSettingValue
    {
        reflection::ReflectionValue value;
        SettingValueSource source = SettingValueSource::CompiledDefault;
    };

    struct ResolvedSettingsSection
    {
        std::string key;
        uint64_t revision = 0;
        std::map<std::string, ResolvedSettingValue, std::less<>> properties;
    };

    struct SettingsResolutionDiagnostic
    {
        std::string section_key;
        SettingValueSource source = SettingValueSource::CompiledDefault;
        std::string message;
    };

    struct SettingsResolvedSnapshot
    {
        uint64_t revision = 0;
        std::vector<ResolvedSettingsSection> sections;
        std::vector<SettingsResolutionDiagnostic> diagnostics;
    };

    enum class SettingsEditStatus : uint8_t
    {
        Pending,
        Applied,
        Rejected,
        Superseded,
        Cancelled,
        RestartRequired,
        Busy,
        StaleRevision,
        ProviderUnavailable,
        InvalidValues,
    };

    struct SettingsEditResult
    {
        uint64_t request_id = 0;
        uint64_t section_revision = 0;
        SettingsEditStatus status = SettingsEditStatus::InvalidValues;
        std::string diagnostic;
        SettingValues active_values;
    };

    enum class SettingApplyPolicy : uint8_t
    {
        Live,
        RestartRequired,
    };

    struct SettingValidationResult
    {
        bool valid = true;
        std::string diagnostic;

        static SettingValidationResult Reject(std::string message)
        {
            return {false, std::move(message)};
        }
    };

    using SettingSectionValidator =
        std::function<SettingValidationResult(const SettingValues &)>;

    struct SettingMigrationResult
    {
        bool migrated = true;
        SettingValues values;
        std::vector<std::string> remove_keys;
        std::string diagnostic;

        static SettingMigrationResult Reject(std::string message)
        {
            return {false, {}, {}, std::move(message)};
        }
    };

    using SettingSectionMigration =
        std::function<SettingMigrationResult(uint32_t, const SettingValues &)>;

    struct SettingPropertyDefinition
    {
        std::string key;
        reflection::ReflectionValueType value_type = reflection::ReflectionValueType::Bool;
        reflection::ReflectionValue default_value;
        reflection::ReflectionPropertyMetadata metadata;
    };

    struct SettingSectionDefinition
    {
        std::string key;
        std::string label;
        std::string category;
        uint32_t schema_version = 1;
        std::vector<SettingPropertyDefinition> properties;
        std::vector<SettingSaveScope> allowed_save_scopes;
        SettingApplyPolicy apply_policy = SettingApplyPolicy::Live;
        SettingSectionValidator validate;
        SettingSectionMigration migrate;
    };

    enum class SettingsResultStatus : uint8_t
    {
        Success,
        InvalidDefinition,
        InvalidValues,
        DuplicateSection,
        Sealed,
    };

    struct SettingsResult
    {
        SettingsResultStatus status = SettingsResultStatus::Success;
        std::string diagnostic;

        bool IsSuccess() const noexcept { return status == SettingsResultStatus::Success; }
        explicit operator bool() const noexcept { return IsSuccess(); }
    };
}

#endif
