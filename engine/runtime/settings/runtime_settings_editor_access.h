#ifndef KPENGINE_RUNTIME_SETTINGS_RUNTIME_SETTINGS_EDITOR_ACCESS_H
#define KPENGINE_RUNTIME_SETTINGS_RUNTIME_SETTINGS_EDITOR_ACCESS_H

#include <deque>
#include <map>
#include <mutex>

#include "i_settings_editor_access.h"

namespace kpengine::settings
{
    class RuntimeSettingsEditorAccess final : public ISettingsEditorAccess
    {
    public:
        RuntimeSettingsEditorAccess(SettingsRegistry &registry,
                                    SettingsStore &project_store,
                                    SettingsStore &user_store);

        std::uint64_t GetRevision() const override;
        SettingsEditorSnapshot CaptureSnapshot() const override;
        SettingsEditResult SubmitEdit(std::string_view section_key,
                                      std::uint64_t base_revision,
                                      SettingValues changed_values) override;
        std::optional<SettingsEditResult> GetEditResult(
            std::uint64_t request_id) override;
        SettingsSaveRequestResult SubmitSave(SettingSaveScope scope,
                                             std::string_view section_key) override;
        std::optional<SettingsSaveRequestResult> GetSaveResult(
            std::uint64_t request_id) const override;

        // Called on Runtime's game lane; disk writes never run in the ImGui frame.
        std::size_t PumpGameLane(std::size_t maximum_requests = 4);

    private:
        SettingsRegistry &registry_;
        SettingsStore &project_store_;
        SettingsStore &user_store_;
        mutable std::mutex mutex_;
        std::deque<SettingsSaveRequest> pending_saves_;
        std::map<std::uint64_t, SettingsSaveRequestResult> save_results_;
        std::uint64_t next_save_request_id_ = 1;
    };
}

#endif
