#ifndef KPENGINE_RUNTIME_SETTINGS_SETTINGS_STORE_H
#define KPENGINE_RUNTIME_SETTINGS_SETTINGS_STORE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "settings_registry.h"

namespace kpengine::settings
{
    enum class SettingsStoreStatus : uint8_t
    {
        Success,
        MissingFile,
        NotLoaded,
        RegistryNotSealed,
        InvalidDocument,
        UnsupportedVersion,
        InvalidValues,
        ScopeNotAllowed,
        IoError,
        DiskConflict,
        ReadOnly,
    };

    struct SettingsStoreResult
    {
        SettingsStoreStatus status = SettingsStoreStatus::Success;
        std::string diagnostic;

        bool IsSuccess() const noexcept
        {
            return status == SettingsStoreStatus::Success ||
                   status == SettingsStoreStatus::MissingFile;
        }
        explicit operator bool() const noexcept { return IsSuccess(); }
    };

    struct SettingsStoreDiagnostic
    {
        std::string section_key;
        std::string message;
    };

    struct SettingsStoreSnapshot
    {
        SettingSaveScope scope = SettingSaveScope::Project;
        SettingsLayerOverrides overrides;
        std::vector<SettingsStoreDiagnostic> diagnostics;
        bool loaded = false;
        bool writable = false;
    };

    // One instance represents one explicit project or user settings file.
    class SettingsStore final
    {
    public:
        SettingsStore(std::filesystem::path path, SettingSaveScope scope);
        ~SettingsStore();

        SettingsStore(const SettingsStore &) = delete;
        SettingsStore &operator=(const SettingsStore &) = delete;
        SettingsStore(SettingsStore &&) noexcept;
        SettingsStore &operator=(SettingsStore &&) noexcept;

        SettingsStoreResult Load(const SettingsRegistry &registry);
        SettingsStoreSnapshot GetSnapshot() const;
        SettingsStoreResult Save(const SettingsRegistry &registry,
                                 const SettingsLayerOverrides &accepted_overrides);

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}

#endif
