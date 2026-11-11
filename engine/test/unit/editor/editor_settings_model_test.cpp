#include "editor_settings_model.h"

#include <gtest/gtest.h>

namespace kpengine::editor
{
    namespace
    {
        class FakeSettingsAccess final : public settings::ISettingsEditorAccess
        {
        public:
            FakeSettingsAccess()
            {
                settings::SettingsEditorSection section;
                section.key = "render.path_tracing";
                section.label = "Path tracing";
                section.category = "Rendering";
                section.apply_policy = settings::SettingApplyPolicy::Live;
                section.allowed_save_scopes = {settings::SettingSaveScope::Project,
                                               settings::SettingSaveScope::User};
                section.properties.push_back(
                    {"enabled", reflection::ReflectionValueType::Bool, false,
                     {.display_name = "Enabled"}});
                section.properties.push_back(
                    {"quality", reflection::ReflectionValueType::UnsignedInteger,
                     uint64_t{2}, {.display_name = "Quality", .minimum = 1.0,
                                   .maximum = 8.0, .step = 1.0}});
                snapshot_.sections.push_back(std::move(section));
                snapshot_.provider_available.emplace("render.path_tracing", true);
                SetValues(false, uint64_t{2}, 1);
            }

            uint64_t GetRevision() const override { return snapshot_.revision; }
            settings::SettingsEditorSnapshot CaptureSnapshot() const override
            {
                return snapshot_;
            }
            settings::SettingsEditResult SubmitEdit(
                std::string_view section_key, uint64_t base_revision,
                settings::SettingValues changed_values) override
            {
                ++submit_count;
                submitted_revision = base_revision;
                submitted_values = std::move(changed_values);
                if (base_revision != snapshot_.revision)
                {
                    return {0, snapshot_.revision, settings::SettingsEditStatus::StaleRevision,
                            "revision changed"};
                }
                if (next_edit_status == settings::SettingsEditStatus::Pending)
                {
                    pending_edit_id = next_request_id++;
                    edit_result = settings::SettingsEditResult{
                        pending_edit_id, base_revision,
                        settings::SettingsEditStatus::Pending, {}};
                    return edit_result;
                }
                if (next_edit_status == settings::SettingsEditStatus::RestartRequired)
                {
                    SetResolvedValue(section_key, submitted_values);
                    ++snapshot_.revision;
                    snapshot_.resolved.revision = snapshot_.revision;
                    return {0, snapshot_.revision, next_edit_status,
                            "restart required"};
                }
                return {0, base_revision, next_edit_status, "edit rejected"};
            }
            std::optional<settings::SettingsEditResult> GetEditResult(uint64_t request_id) override
            {
                if (request_id == pending_edit_id)
                {
                    return edit_result;
                }
                return std::nullopt;
            }
            settings::SettingsSaveRequestResult SubmitSave(
                settings::SettingSaveScope scope, std::string_view) override
            {
                pending_save_id = next_request_id++;
                save_result = {pending_save_id, scope,
                               settings::SettingsSaveRequestStatus::Pending, "queued"};
                return save_result;
            }
            std::optional<settings::SettingsSaveRequestResult> GetSaveResult(
                uint64_t request_id) const override
            {
                if (request_id == pending_save_id)
                {
                    return save_result;
                }
                return std::nullopt;
            }

            void CompletePending(bool accepted)
            {
                if (accepted)
                {
                    SetResolvedValue("render.path_tracing", submitted_values);
                    ++snapshot_.revision;
                    snapshot_.resolved.revision = snapshot_.revision;
                    edit_result.status = settings::SettingsEditStatus::Applied;
                    edit_result.section_revision = snapshot_.revision;
                    edit_result.diagnostic = "applied";
                }
                else
                {
                    edit_result.status = settings::SettingsEditStatus::Rejected;
                    edit_result.diagnostic = "provider rejected";
                }
            }

            void CompleteSave(bool success)
            {
                save_result.status = success ? settings::SettingsSaveRequestStatus::Succeeded
                                             : settings::SettingsSaveRequestStatus::Failed;
                save_result.diagnostic = success ? "saved" : "write failed";
                if (success)
                {
                    ++snapshot_.revision;
                    snapshot_.resolved.revision = snapshot_.revision;
                }
            }

            void SetExternalValue(bool enabled)
            {
                SetValues(enabled, uint64_t{2}, snapshot_.revision + 1);
            }

            settings::SettingsEditStatus next_edit_status =
                settings::SettingsEditStatus::Pending;
            uint64_t submitted_revision = 0;
            std::size_t submit_count = 0;
            settings::SettingValues submitted_values;

        private:
            void SetResolvedValue(std::string_view section_key,
                                  const settings::SettingValues &values)
            {
                for (settings::ResolvedSettingsSection &section : snapshot_.resolved.sections)
                {
                    if (section.key != section_key)
                    {
                        continue;
                    }
                    for (const auto &[key, value] : values)
                    {
                        section.properties[key].value = value;
                    }
                    section.revision = snapshot_.revision + 1;
                }
            }

            void SetValues(bool enabled, uint64_t quality, uint64_t revision)
            {
                snapshot_.revision = revision;
                snapshot_.resolved.revision = revision;
                snapshot_.resolved.sections.clear();
                settings::ResolvedSettingsSection section;
                section.key = "render.path_tracing";
                section.revision = revision;
                section.properties.emplace(
                    "enabled", settings::ResolvedSettingValue{enabled,
                                                                 settings::SettingValueSource::User});
                section.properties.emplace(
                    "quality", settings::ResolvedSettingValue{quality,
                                                                settings::SettingValueSource::Project});
                snapshot_.resolved.sections.push_back(std::move(section));
            }

            settings::SettingsEditorSnapshot snapshot_;
            uint64_t next_request_id = 1;
            uint64_t pending_edit_id = 0;
            uint64_t pending_save_id = 0;
            settings::SettingsEditResult edit_result;
            settings::SettingsSaveRequestResult save_result;
        };
    }

    TEST(SettingsEditorModelTest, DraftRevertDefaultsAndSearchUseCopiedMetadata)
    {
        FakeSettingsAccess access;
        SettingsEditorModel model;
        model.Refresh(access);
        EXPECT_EQ(model.SelectedSection(), "render.path_tracing");
        EXPECT_TRUE(model.MatchesSearch(model.Snapshot().sections.front()));
        model.SetSearch("quality");
        EXPECT_TRUE(model.MatchesSearch(model.Snapshot().sections.front()));
        model.SetSearch("missing-term");
        EXPECT_FALSE(model.MatchesSearch(model.Snapshot().sections.front()));
        model.SetSearch({});

        EXPECT_TRUE(model.SetValue("enabled", true));
        EXPECT_TRUE(model.HasDrafts());
        model.Revert("render.path_tracing");
        EXPECT_FALSE(model.HasDrafts());

        EXPECT_TRUE(model.SetValue("enabled", true));
        model.RestoreDefaults("render.path_tracing");
        EXPECT_FALSE(model.HasDrafts());
    }

    TEST(SettingsEditorModelTest, PendingEditBecomesAppliedAndRefreshesRevision)
    {
        FakeSettingsAccess access;
        SettingsEditorModel model;
        model.Refresh(access);
        ASSERT_TRUE(model.SetValue("enabled", true));
        const auto submitted = model.Apply(access, "render.path_tracing");
        ASSERT_EQ(submitted.status, settings::SettingsEditStatus::Pending);
        EXPECT_EQ(model.Draft("render.path_tracing")->state,
                  EditorSettingsEditState::Pending);
        EXPECT_FALSE(model.HasDrafts());
        EXPECT_TRUE(model.HasPending());

        access.CompletePending(true);
        model.Poll(access);
        EXPECT_FALSE(model.HasDrafts());
        EXPECT_EQ(model.Draft("render.path_tracing")->base_revision, 2u);
        EXPECT_EQ(model.Draft("render.path_tracing")->values.at("enabled"), true);
    }

    TEST(SettingsEditorModelTest, StaleEditPreservesDraftUntilExplicitRebase)
    {
        FakeSettingsAccess access;
        SettingsEditorModel model;
        model.Refresh(access);
        ASSERT_TRUE(model.SetValue("enabled", true));
        access.SetExternalValue(false);
        model.Refresh(access);
        EXPECT_EQ(model.Draft("render.path_tracing")->state,
                  EditorSettingsEditState::Conflict);

        ASSERT_TRUE(model.Rebase("render.path_tracing"));
        EXPECT_EQ(model.Draft("render.path_tracing")->base_revision, 2u);
        EXPECT_EQ(model.Draft("render.path_tracing")->values.at("enabled"), true);
        EXPECT_TRUE(model.HasDrafts());
    }

    TEST(SettingsEditorModelTest, RestoreDefaultsKeepsConflictAndApplyAllSkipsIt)
    {
        FakeSettingsAccess access;
        SettingsEditorModel model;
        model.Refresh(access);
        ASSERT_TRUE(model.SetValue("enabled", true));
        access.SetExternalValue(false);
        model.Refresh(access);

        model.RestoreDefaults("render.path_tracing");
        EXPECT_EQ(model.Draft("render.path_tracing")->state,
                  EditorSettingsEditState::Conflict);
        const std::size_t before = access.submit_count;
        EXPECT_EQ(model.ApplyAll(access), 0u);
        EXPECT_EQ(access.submit_count, before);
    }

    TEST(SettingsEditorModelTest, RestartIntentAndSaveFailuresRemainVisible)
    {
        FakeSettingsAccess access;
        access.next_edit_status = settings::SettingsEditStatus::RestartRequired;
        SettingsEditorModel model;
        model.Refresh(access);
        ASSERT_TRUE(model.SetValue("enabled", true));
        const auto applied = model.Apply(access, "render.path_tracing");
        EXPECT_EQ(applied.status, settings::SettingsEditStatus::RestartRequired);
        EXPECT_FALSE(model.HasDrafts());

        const auto save = model.Save(access, settings::SettingSaveScope::User,
                                     "render.path_tracing");
        ASSERT_EQ(save.status, settings::SettingsSaveRequestStatus::Pending);
        access.CompleteSave(false);
        model.Poll(access);
        ASSERT_TRUE(model.LastSaveResult().has_value());
        EXPECT_EQ(model.LastSaveResult()->status,
                  settings::SettingsSaveRequestStatus::Failed);
        EXPECT_EQ(model.LastSaveResult()->diagnostic, "write failed");
    }
}
