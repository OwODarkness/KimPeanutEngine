#ifndef KPENGINE_RUNTIME_SETTINGS_SETTINGS_REGISTRY_H
#define KPENGINE_RUNTIME_SETTINGS_SETTINGS_REGISTRY_H

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "i_settings_provider.h"
#include "settings_types.h"

namespace kpengine::settings
{
    struct SettingsProviderBindResult
    {
        SettingsResult result;
        SettingsProviderRegistration registration;

        bool IsSuccess() const noexcept { return result.IsSuccess(); }
        explicit operator bool() const noexcept { return IsSuccess(); }
    };

    // Owns the schema catalog, resolved layers, and queued provider requests.
    class SettingsRegistry final
    {
    public:
        SettingsRegistry();
        ~SettingsRegistry();

        SettingsResult RegisterSection(SettingSectionDefinition section);
        SettingsResult Seal();

        bool IsSealed() const;
        std::optional<SettingSectionDefinition> FindSection(std::string_view key) const;
        std::vector<SettingSectionDefinition> GetSections() const;
        SettingsResult ValidateValues(std::string_view section_key,
                                      const SettingValues &values) const;

        SettingsResult SetPersistentOverrides(SettingSaveScope scope,
                                              SettingsLayerOverrides overrides);
        SettingsResult SetSessionOverrides(SettingsLayerOverrides overrides);
        SettingsResult RemovePersistentOverride(SettingSaveScope scope,
                                                std::string_view section_key,
                                                std::string_view property_key);
        SettingsResult RemoveSessionOverride(std::string_view section_key,
                                             std::string_view property_key);
        SettingsLayerOverrides GetPersistentOverrides(SettingSaveScope scope) const;
        SettingsLayerOverrides GetSessionOverrides() const;
        SettingsResolvedSnapshot GetResolvedSnapshot() const;

        SettingsProviderBindResult BindProvider(std::string_view section_key,
                                                ISettingsProvider &provider);
        SettingsEditResult SubmitEdit(std::string_view section_key,
                                      uint64_t base_revision,
                                      SettingValues changed_values,
                                      SettingsEditOrigin origin);
        std::size_t PumpProviders(std::size_t maximum_requests = 16);
        std::optional<SettingsEditResult> GetEditResult(uint64_t request_id);

    private:
        struct CompletionControl;
        struct ProviderBinding
        {
            std::shared_ptr<detail::SettingsProviderRegistrationState> registration;
        };
        struct RequestRecord
        {
            SettingsApplyRequest request;
            SettingsEditResult result;
        };

        void SweepInactiveProvidersLocked();
        bool TrimRequestResultsLocked();
        void MarkSectionRevisionsLocked(const std::vector<std::string> &section_keys);
        bool CompleteProviderEdit(uint64_t generation,
                                  uint64_t request_id,
                                  SettingsProviderResult result);
        SettingsCompletionHandle MakeCompletionHandle(uint64_t generation,
                                                       uint64_t request_id);

        mutable std::mutex mutex_;
        std::map<std::string, SettingSectionDefinition, std::less<>> sections_;
        SettingsLayerOverrides project_overrides_;
        SettingsLayerOverrides user_overrides_;
        SettingsLayerOverrides session_overrides_;
        uint64_t values_revision_ = 0;
        std::map<std::string, uint64_t, std::less<>> section_revisions_;
        std::map<std::string, ProviderBinding, std::less<>> providers_;
        std::map<uint64_t, RequestRecord> requests_;
        std::deque<uint64_t> provider_queue_;
        std::map<std::string, uint64_t, std::less<>> section_in_flight_;
        uint64_t next_provider_generation_ = 1;
        uint64_t next_request_id_ = 1;
        std::shared_ptr<CompletionControl> completion_control_;
        bool sealed_ = false;
    };
}

#endif
