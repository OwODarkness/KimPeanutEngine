#include "runtime_settings_editor_access.h"

#include <algorithm>
#include <utility>

namespace kpengine::settings
{
    namespace
    {
        constexpr std::size_t kMaximumPendingSaves = 16;
        constexpr std::size_t kMaximumSaveResults = 64;

        SettingsEditorSection CopyEditorSection(const SettingSectionDefinition &section)
        {
            return {section.key, section.label, section.category, section.schema_version,
                    section.properties, section.allowed_save_scopes, section.apply_policy};
        }

        const ResolvedSettingsSection *FindResolvedSection(
            const SettingsResolvedSnapshot &snapshot, std::string_view key)
        {
            const auto found = std::find_if(
                snapshot.sections.begin(), snapshot.sections.end(),
                [key](const ResolvedSettingsSection &section) { return section.key == key; });
            return found == snapshot.sections.end() ? nullptr : &*found;
        }

        SettingsSaveRequestResult SaveFailure(std::uint64_t request_id,
                                              SettingSaveScope scope,
                                              std::string diagnostic)
        {
            return {request_id, scope, SettingsSaveRequestStatus::Failed,
                    std::move(diagnostic)};
        }
    }

    RuntimeSettingsEditorAccess::RuntimeSettingsEditorAccess(
        SettingsRegistry &registry, SettingsStore &project_store,
        SettingsStore &user_store)
        : registry_(registry), project_store_(project_store), user_store_(user_store)
    {
    }

    std::uint64_t RuntimeSettingsEditorAccess::GetRevision() const
    {
        return registry_.GetValuesRevision();
    }

    SettingsEditorSnapshot RuntimeSettingsEditorAccess::CaptureSnapshot() const
    {
        SettingsEditorSnapshot snapshot;
        snapshot.resolved = registry_.GetResolvedSnapshot();
        snapshot.revision = snapshot.resolved.revision;
        snapshot.diagnostics = snapshot.resolved.diagnostics;
        for (const SettingSectionDefinition &section : registry_.GetSections())
        {
            snapshot.sections.push_back(CopyEditorSection(section));
            snapshot.provider_available.emplace(section.key, registry_.HasProvider(section.key));
        }
        {
            std::lock_guard lock(mutex_);
            snapshot.project_store = project_store_.GetSnapshot();
            snapshot.user_store = user_store_.GetSnapshot();
        }
        return snapshot;
    }

    SettingsEditResult RuntimeSettingsEditorAccess::SubmitEdit(
        std::string_view section_key, std::uint64_t base_revision,
        SettingValues changed_values)
    {
        return registry_.SubmitEdit(section_key, base_revision, std::move(changed_values),
                                    SettingsEditOrigin::Editor);
    }

    std::optional<SettingsEditResult> RuntimeSettingsEditorAccess::GetEditResult(
        std::uint64_t request_id)
    {
        return registry_.GetEditResult(request_id);
    }

    SettingsSaveRequestResult RuntimeSettingsEditorAccess::SubmitSave(
        SettingSaveScope scope, std::string_view section_key)
    {
        if (scope != SettingSaveScope::Project && scope != SettingSaveScope::User)
        {
            return SaveFailure(0, scope, "choose a Project or User save scope");
        }
        const auto section = registry_.FindSection(section_key);
        if (!section)
        {
            return SaveFailure(0, scope, "settings section is no longer registered");
        }
        if (std::find(section->allowed_save_scopes.begin(), section->allowed_save_scopes.end(),
                      scope) == section->allowed_save_scopes.end())
        {
            return SaveFailure(0, scope, "settings section does not allow this save scope");
        }

        std::lock_guard lock(mutex_);
        if (pending_saves_.size() >= kMaximumPendingSaves)
        {
            return SaveFailure(0, scope, "settings save queue is full");
        }
        const std::uint64_t request_id = next_save_request_id_++;
        pending_saves_.push_back({request_id, scope, std::string{section_key}});
        save_results_.emplace(
            request_id,
            SettingsSaveRequestResult{request_id, scope, SettingsSaveRequestStatus::Pending,
                                      "waiting for Runtime save"});
        return save_results_.at(request_id);
    }

    std::optional<SettingsSaveRequestResult> RuntimeSettingsEditorAccess::GetSaveResult(
        std::uint64_t request_id) const
    {
        std::lock_guard lock(mutex_);
        const auto result = save_results_.find(request_id);
        return result == save_results_.end()
                   ? std::nullopt
                   : std::optional<SettingsSaveRequestResult>{result->second};
    }

    std::size_t RuntimeSettingsEditorAccess::PumpGameLane(std::size_t maximum_requests)
    {
        std::size_t processed = 0;
        while (processed < maximum_requests)
        {
            SettingsSaveRequest request;
            {
                std::lock_guard lock(mutex_);
                if (pending_saves_.empty())
                {
                    break;
                }
                request = std::move(pending_saves_.front());
                pending_saves_.pop_front();
            }

            SettingsSaveRequestResult result{request.request_id, request.scope,
                                             SettingsSaveRequestStatus::Failed, {}};
            const SettingsResolvedSnapshot resolved = registry_.GetResolvedSnapshot();
            const ResolvedSettingsSection *const section =
                FindResolvedSection(resolved, request.section_key);
            if (section == nullptr)
            {
                result.diagnostic = "settings section is no longer available";
            }
            else
            {
                SettingValues accepted_values;
                for (const auto &[key, property] : section->properties)
                {
                    accepted_values.emplace(key, property.value);
                }
                SettingsLayerOverrides accepted = registry_.GetPersistentOverrides(request.scope);
                accepted[request.section_key] = std::move(accepted_values);
                SettingsStoreResult save;
                {
                    std::lock_guard lock(mutex_);
                    SettingsStore &store = request.scope == SettingSaveScope::Project
                                               ? project_store_
                                               : user_store_;
                    save = store.Save(registry_, accepted);
                }
                if (!save)
                {
                    result.diagnostic = std::move(save.diagnostic);
                }
                else
                {
                    const SettingsResult layer_update =
                        registry_.SetPersistentOverrides(request.scope, std::move(accepted));
                    result.status = layer_update ? SettingsSaveRequestStatus::Succeeded
                                                 : SettingsSaveRequestStatus::Failed;
                    result.diagnostic = layer_update
                                            ? "accepted settings saved"
                                            : layer_update.diagnostic;
                }
            }

            {
                std::lock_guard lock(mutex_);
                save_results_.insert_or_assign(request.request_id, std::move(result));
                while (save_results_.size() > kMaximumSaveResults)
                {
                    const auto completed = std::find_if(
                        save_results_.begin(), save_results_.end(),
                        [](const auto &entry)
                        {
                            return entry.second.status != SettingsSaveRequestStatus::Pending;
                        });
                    if (completed == save_results_.end())
                    {
                        break;
                    }
                    save_results_.erase(completed);
                }
            }
            ++processed;
        }
        return processed;
    }
}
