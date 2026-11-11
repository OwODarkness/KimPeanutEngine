#include "editor_settings_model.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace kpengine::editor
{
    namespace
    {
        std::string Lower(std::string_view value)
        {
            std::string result{value};
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
            return result;
        }

        settings::SettingValues ResolvedValues(
            const settings::ResolvedSettingsSection *section)
        {
            settings::SettingValues values;
            if (section == nullptr)
            {
                return values;
            }
            for (const auto &[key, value] : section->properties)
            {
                values.emplace(key, value.value);
            }
            return values;
        }

        settings::SettingValues ChangedValues(const EditorSettingsDraft &draft)
        {
            settings::SettingValues changed;
            for (const auto &[key, value] : draft.values)
            {
                const auto base = draft.base_values.find(key);
                if (base == draft.base_values.end() || base->second != value)
                {
                    changed.emplace(key, value);
                }
            }
            return changed;
        }
    }

    void SettingsEditorModel::Refresh(settings::ISettingsEditorAccess &access)
    {
        if (snapshot_.revision == access.GetRevision() && !snapshot_.sections.empty())
        {
            Poll(access);
            return;
        }
        snapshot_ = access.CaptureSnapshot();
        RefreshCleanDrafts();
        if (selected_section_.empty() && !snapshot_.sections.empty())
        {
            const auto path_tracing = std::find_if(
                snapshot_.sections.begin(), snapshot_.sections.end(),
                [](const settings::SettingsEditorSection &section)
                {
                    return section.key == "render.path_tracing";
                });
            selected_section_ = path_tracing != snapshot_.sections.end()
                                    ? path_tracing->key
                                    : snapshot_.sections.front().key;
        }
        Poll(access);
    }

    void SettingsEditorModel::SelectSection(std::string_view section_key)
    {
        if (FindSchema(section_key) != nullptr)
        {
            selected_section_ = section_key;
        }
    }

    void SettingsEditorModel::SetSearch(std::string search)
    {
        search_ = std::move(search);
    }

    bool SettingsEditorModel::MatchesSearch(
        const settings::SettingsEditorSection &section) const
    {
        if (search_.empty())
        {
            return true;
        }
        const std::string needle = Lower(search_);
        const auto contains = [&needle](std::string_view candidate)
        {
            return Lower(candidate).find(needle) != std::string::npos;
        };
        if (contains(section.category) || contains(section.label) || contains(section.key))
        {
            return true;
        }
        return std::any_of(section.properties.begin(), section.properties.end(),
                           [&contains](const settings::SettingPropertyDefinition &property)
                           {
                               return contains(property.key) ||
                                      contains(property.metadata.display_name) ||
                                      contains(property.metadata.tooltip) ||
                                      contains(property.metadata.category);
                           });
    }

    bool SettingsEditorModel::SetValue(
        std::string_view property_key, const reflection::ReflectionValue &value)
    {
        EditorSettingsDraft *const draft = Draft(selected_section_);
        if (draft == nullptr || draft->pending_request.has_value())
        {
            return false;
        }
        const auto property = draft->values.find(property_key);
        if (property == draft->values.end() || property->second.GetType() != value.GetType())
        {
            return false;
        }
        property->second = value;
        draft->diagnostic.clear();
        const settings::ResolvedSettingsSection *const current = FindResolved(selected_section_);
        draft->state = current != nullptr && current->revision != draft->base_revision
                           ? EditorSettingsEditState::Conflict
                           : ChangedValues(*draft).empty() ? EditorSettingsEditState::Clean
                                                           : EditorSettingsEditState::Dirty;
        return true;
    }

    void SettingsEditorModel::Revert(std::string_view section_key)
    {
        EditorSettingsDraft *const draft = Draft(section_key);
        const settings::SettingsEditorSection *const schema = FindSchema(section_key);
        if (draft == nullptr || schema == nullptr || draft->pending_request.has_value())
        {
            return;
        }
        InitializeDraft(*schema, FindResolved(section_key), *draft);
    }

    void SettingsEditorModel::RestoreDefaults(std::string_view section_key)
    {
        EditorSettingsDraft *const draft = Draft(section_key);
        const settings::SettingsEditorSection *const schema = FindSchema(section_key);
        if (draft == nullptr || schema == nullptr || draft->pending_request.has_value())
        {
            return;
        }
        for (const settings::SettingPropertyDefinition &property : schema->properties)
        {
            draft->values.insert_or_assign(property.key, property.default_value);
        }
        draft->diagnostic.clear();
        const settings::ResolvedSettingsSection *const current = FindResolved(section_key);
        if (current != nullptr && current->revision != draft->base_revision)
        {
            draft->state = EditorSettingsEditState::Conflict;
        }
        else
        {
            draft->state = ChangedValues(*draft).empty() ? EditorSettingsEditState::Clean
                                                         : EditorSettingsEditState::Dirty;
        }
    }

    settings::SettingsEditResult SettingsEditorModel::Apply(
        settings::ISettingsEditorAccess &access, std::string_view section_key)
    {
        EditorSettingsDraft *const draft = Draft(section_key);
        if (draft == nullptr || draft->pending_request.has_value())
        {
            return {0, 0, settings::SettingsEditStatus::Busy,
                    "settings edit is already pending"};
        }
        settings::SettingValues changed = ChangedValues(*draft);
        if (changed.empty())
        {
            draft->state = EditorSettingsEditState::Clean;
            return {0, draft->base_revision, settings::SettingsEditStatus::Applied, {}};
        }

        const settings::SettingsEditResult result =
            access.SubmitEdit(section_key, draft->base_revision, std::move(changed));
        draft->diagnostic = result.diagnostic;
        switch (result.status)
        {
        case settings::SettingsEditStatus::Pending:
            draft->pending_request = result.request_id;
            draft->state = EditorSettingsEditState::Pending;
            break;
        case settings::SettingsEditStatus::RestartRequired:
            draft->state = EditorSettingsEditState::RestartRequired;
            Refresh(access);
            break;
        case settings::SettingsEditStatus::Applied:
            draft->state = EditorSettingsEditState::Applied;
            Refresh(access);
            break;
        case settings::SettingsEditStatus::StaleRevision:
            draft->state = EditorSettingsEditState::Conflict;
            Refresh(access);
            break;
        case settings::SettingsEditStatus::ProviderUnavailable:
            draft->state = EditorSettingsEditState::Unavailable;
            break;
        case settings::SettingsEditStatus::Rejected:
        case settings::SettingsEditStatus::Superseded:
        case settings::SettingsEditStatus::Cancelled:
        case settings::SettingsEditStatus::Busy:
        case settings::SettingsEditStatus::InvalidValues:
            draft->state = EditorSettingsEditState::Rejected;
            break;
        }
        return result;
    }

    std::size_t SettingsEditorModel::ApplyAll(settings::ISettingsEditorAccess &access)
    {
        std::vector<std::string> dirty_sections;
        for (auto &[key, draft] : drafts_)
        {
            if (!ChangedValues(draft).empty() && !draft.pending_request.has_value() &&
                draft.state != EditorSettingsEditState::Conflict)
            {
                dirty_sections.push_back(key);
            }
        }
        std::size_t submitted = 0;
        for (const std::string &key : dirty_sections)
        {
            const settings::SettingsEditResult result = Apply(access, key);
            if (result.status == settings::SettingsEditStatus::Pending ||
                result.status == settings::SettingsEditStatus::RestartRequired ||
                result.status == settings::SettingsEditStatus::Applied)
            {
                ++submitted;
            }
        }
        return submitted;
    }

    bool SettingsEditorModel::Rebase(std::string_view section_key)
    {
        EditorSettingsDraft *const draft = Draft(section_key);
        const settings::SettingsEditorSection *const schema = FindSchema(section_key);
        const settings::ResolvedSettingsSection *const resolved = FindResolved(section_key);
        if (draft == nullptr || schema == nullptr || resolved == nullptr ||
            draft->pending_request.has_value())
        {
            return false;
        }
        const settings::SettingValues changed = ChangedValues(*draft);
        draft->base_values = ResolvedValues(resolved);
        draft->values = draft->base_values;
        for (const auto &[key, value] : changed)
        {
            draft->values.insert_or_assign(key, value);
        }
        draft->base_revision = resolved->revision;
        draft->state = ChangedValues(*draft).empty() ? EditorSettingsEditState::Clean
                                                     : EditorSettingsEditState::Dirty;
        draft->diagnostic.clear();
        return true;
    }

    settings::SettingsSaveRequestResult SettingsEditorModel::Save(
        settings::ISettingsEditorAccess &access, settings::SettingSaveScope scope,
        std::string_view section_key)
    {
        if (pending_save_request_.has_value())
        {
            return {0, scope, settings::SettingsSaveRequestStatus::Pending,
                    "a settings save is already pending"};
        }
        last_save_result_ = access.SubmitSave(scope, section_key);
        if (last_save_result_->status == settings::SettingsSaveRequestStatus::Pending)
        {
            pending_save_request_ = last_save_result_->request_id;
        }
        return *last_save_result_;
    }

    void SettingsEditorModel::Poll(settings::ISettingsEditorAccess &access)
    {
        bool refresh_snapshot = false;
        for (auto &[key, draft] : drafts_)
        {
            (void)key;
            if (!draft.pending_request.has_value())
            {
                continue;
            }
            const auto result = access.GetEditResult(*draft.pending_request);
            if (!result || result->status == settings::SettingsEditStatus::Pending)
            {
                continue;
            }
            draft.pending_request.reset();
            refresh_snapshot = true;
            draft.diagnostic = result->diagnostic;
            switch (result->status)
            {
            case settings::SettingsEditStatus::Applied:
                draft.state = EditorSettingsEditState::Applied;
                break;
            case settings::SettingsEditStatus::StaleRevision:
            case settings::SettingsEditStatus::Superseded:
                draft.state = EditorSettingsEditState::Conflict;
                break;
            case settings::SettingsEditStatus::ProviderUnavailable:
                draft.state = EditorSettingsEditState::Unavailable;
                break;
            case settings::SettingsEditStatus::RestartRequired:
                draft.state = EditorSettingsEditState::RestartRequired;
                break;
            case settings::SettingsEditStatus::Pending:
                break;
            case settings::SettingsEditStatus::Rejected:
            case settings::SettingsEditStatus::Cancelled:
            case settings::SettingsEditStatus::Busy:
            case settings::SettingsEditStatus::InvalidValues:
                draft.state = EditorSettingsEditState::Rejected;
                break;
            }
        }
        if (pending_save_request_.has_value())
        {
            const auto result = access.GetSaveResult(*pending_save_request_);
            if (result && result->status != settings::SettingsSaveRequestStatus::Pending)
            {
                pending_save_request_.reset();
                last_save_result_ = *result;
                if (result->status == settings::SettingsSaveRequestStatus::Succeeded)
                {
                    refresh_snapshot = true;
                }
            }
        }
        if (refresh_snapshot)
        {
            snapshot_ = access.CaptureSnapshot();
            RefreshCleanDrafts();
        }
    }

    const EditorSettingsDraft *SettingsEditorModel::Draft(
        std::string_view section_key) const noexcept
    {
        const auto found = drafts_.find(section_key);
        return found == drafts_.end() ? nullptr : &found->second;
    }

    EditorSettingsDraft *SettingsEditorModel::Draft(std::string_view section_key) noexcept
    {
        const auto found = drafts_.find(section_key);
        return found == drafts_.end() ? nullptr : &found->second;
    }

    bool SettingsEditorModel::HasDrafts() const noexcept
    {
        return std::any_of(drafts_.begin(), drafts_.end(),
                           [](const auto &entry)
                           {
                               const EditorSettingsDraft &draft = entry.second;
                               if (draft.pending_request.has_value())
                               {
                                   return false;
                               }
                               for (const auto &[key, value] : draft.values)
                               {
                                   const auto base = draft.base_values.find(key);
                                   if (base == draft.base_values.end() || base->second != value)
                                   {
                                       return true;
                                   }
                               }
                               return false;
                           });
    }

    bool SettingsEditorModel::HasPending() const noexcept
    {
        return std::any_of(drafts_.begin(), drafts_.end(),
                           [](const auto &entry)
                           {
                               return entry.second.pending_request.has_value();
                           }) || pending_save_request_.has_value();
    }

    const settings::SettingsEditorSection *SettingsEditorModel::FindSchema(
        std::string_view section_key) const
    {
        const auto found = std::find_if(
            snapshot_.sections.begin(), snapshot_.sections.end(),
            [section_key](const settings::SettingsEditorSection &section)
            {
                return section.key == section_key;
            });
        return found == snapshot_.sections.end() ? nullptr : &*found;
    }

    const settings::ResolvedSettingsSection *SettingsEditorModel::FindResolved(
        std::string_view section_key) const
    {
        const auto found = std::find_if(
            snapshot_.resolved.sections.begin(), snapshot_.resolved.sections.end(),
            [section_key](const settings::ResolvedSettingsSection &section)
            {
                return section.key == section_key;
            });
        return found == snapshot_.resolved.sections.end() ? nullptr : &*found;
    }

    void SettingsEditorModel::InitializeDraft(
        const settings::SettingsEditorSection &schema,
        const settings::ResolvedSettingsSection *resolved, EditorSettingsDraft &draft) const
    {
        draft = {};
        if (resolved != nullptr)
        {
            draft.base_revision = resolved->revision;
            draft.base_values = ResolvedValues(resolved);
        }
        else
        {
            for (const settings::SettingPropertyDefinition &property : schema.properties)
            {
                draft.base_values.emplace(property.key, property.default_value);
            }
        }
        draft.values = draft.base_values;
        draft.state = EditorSettingsEditState::Clean;
    }

    void SettingsEditorModel::RefreshCleanDrafts()
    {
        for (const settings::SettingsEditorSection &schema : snapshot_.sections)
        {
            EditorSettingsDraft &draft = drafts_[schema.key];
            if (draft.state == EditorSettingsEditState::Clean ||
                draft.state == EditorSettingsEditState::Applied ||
                draft.state == EditorSettingsEditState::RestartRequired)
            {
                InitializeDraft(schema, FindResolved(schema.key), draft);
            }
            else if (!draft.pending_request.has_value())
            {
                const settings::ResolvedSettingsSection *const current = FindResolved(schema.key);
                if (current != nullptr && current->revision != draft.base_revision)
                {
                    draft.state = EditorSettingsEditState::Conflict;
                }
            }
        }
    }
}
