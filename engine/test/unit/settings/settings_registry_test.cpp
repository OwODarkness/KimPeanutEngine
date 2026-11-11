#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "settings/settings_registry.h"
#include "settings/runtime_settings_editor_access.h"
#include "settings/settings_store.h"

namespace
{
    using namespace kpengine::settings;
    using kpengine::reflection::ReflectionEnumOption;
    using kpengine::reflection::ReflectionPropertyMetadata;
    using kpengine::reflection::ReflectionValue;
    using kpengine::reflection::ReflectionValueType;
    using kpengine::reflection::ReflectionWidgetSemantic;
    using Json = nlohmann::json;

    class FakeSettingsProvider final : public ISettingsProvider
    {
    public:
        void Apply(const SettingsApplyRequest &request,
                   SettingsCompletionHandle completion) override
        {
            ++apply_count;
            last_request = request;
            last_completion = completion;
            if (on_apply)
            {
                on_apply();
            }
            if (auto_apply)
            {
                last_completion.Complete(
                    {SettingsProviderOutcome::Applied, request.candidate_values, {}});
            }
        }

        bool Complete(SettingsProviderOutcome outcome,
                      SettingValues active_values = {},
                      std::string diagnostic = {})
        {
            return last_completion.Complete(
                {outcome, std::move(active_values), std::move(diagnostic)});
        }

        uint32_t apply_count = 0;
        bool auto_apply = false;
        SettingsApplyRequest last_request;
        SettingsCompletionHandle last_completion;
        std::function<void()> on_apply;
    };

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            static std::atomic<uint64_t> next_id{0};
            path_ = std::filesystem::current_path() /
                    ("kpengine-settings-" + std::to_string(next_id.fetch_add(1)));
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        const std::filesystem::path &Path() const noexcept { return path_; }

    private:
        std::filesystem::path path_;
    };

    void RegisterStoreSchema(SettingsRegistry &registry,
                             uint32_t version = 1,
                             SettingSectionMigration migration = {})
    {
        SettingPropertyDefinition enabled{};
        enabled.key = "enabled";
        enabled.value_type = ReflectionValueType::Bool;
        enabled.default_value = ReflectionValue{true};
        enabled.metadata.display_name = "Enabled";

        SettingPropertyDefinition samples{};
        samples.key = "samples";
        samples.value_type = ReflectionValueType::SignedInteger;
        samples.default_value = ReflectionValue{4};
        samples.metadata.display_name = "Samples";
        samples.metadata.minimum = 1.0;
        samples.metadata.maximum = 16.0;

        SettingSectionDefinition section{};
        section.key = "render.quality";
        section.label = "Quality";
        section.category = "Rendering";
        section.schema_version = version;
        section.properties = {std::move(enabled), std::move(samples)};
        section.allowed_save_scopes = {SettingSaveScope::Project,
                                       SettingSaveScope::User};
        section.migrate = std::move(migration);
        section.validate = [](const SettingValues &values)
        {
            const bool *enabled_value = values.at("enabled").TryGet<bool>();
            const int64_t *samples_value = values.at("samples").TryGet<int64_t>();
            if (enabled_value != nullptr && !*enabled_value && samples_value != nullptr &&
                *samples_value != 4)
            {
                return SettingValidationResult::Reject(
                    "disabled rendering requires the default sample count");
            }
            return SettingValidationResult{};
        };
        ASSERT_TRUE(registry.RegisterSection(std::move(section)));
        ASSERT_TRUE(registry.Seal());
    }

    void WriteText(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        ASSERT_TRUE(output.good());
    }

    std::string ReadText(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    SettingSectionDefinition MakeSection()
    {
        SettingPropertyDefinition enabled{};
        enabled.key = "enabled";
        enabled.value_type = ReflectionValueType::Bool;
        enabled.default_value = ReflectionValue{true};
        enabled.metadata.display_name = "Enabled";

        SettingPropertyDefinition intensity{};
        intensity.key = "intensity";
        intensity.value_type = ReflectionValueType::FloatingPoint;
        intensity.default_value = ReflectionValue{0.5};
        intensity.metadata.display_name = "Intensity";
        intensity.metadata.minimum = 0.0;
        intensity.metadata.maximum = 1.0;
        intensity.metadata.step = 0.05;

        SettingSectionDefinition section{};
        section.key = "test.rendering";
        section.label = "Rendering";
        section.category = "Graphics";
        section.properties = {std::move(enabled), std::move(intensity)};
        section.allowed_save_scopes = {SettingSaveScope::Project, SettingSaveScope::User};
        return section;
    }

    TEST(SettingsRegistryTest, RegistersAndCopiesAValidSchema)
    {
        SettingsRegistry registry;
        EXPECT_TRUE(registry.RegisterSection(MakeSection()));

        const auto section = registry.FindSection("test.rendering");
        ASSERT_TRUE(section.has_value());
        EXPECT_EQ(section->properties.size(), 2u);
        EXPECT_EQ(section->properties[1].default_value, ReflectionValue{0.5});

        auto snapshot = registry.GetSections();
        ASSERT_EQ(snapshot.size(), 1u);
        snapshot.front().label = "Changed copy";
        EXPECT_EQ(registry.FindSection("test.rendering")->label, "Rendering");
    }

    TEST(SettingsRegistryTest, RejectsDuplicateSectionAndPropertyKeys)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        const SettingsResult duplicate = registry.RegisterSection(MakeSection());
        EXPECT_EQ(duplicate.status, SettingsResultStatus::DuplicateSection);

        auto section = MakeSection();
        section.key = "test.duplicate_property";
        section.properties.push_back(section.properties.front());
        const SettingsResult duplicate_property = registry.RegisterSection(std::move(section));
        EXPECT_EQ(duplicate_property.status, SettingsResultStatus::InvalidDefinition);
    }

    TEST(SettingsRegistryTest, RejectsNonCanonicalKeysAndMismatchedDefaults)
    {
        SettingsRegistry registry;
        auto bad_section = MakeSection();
        bad_section.key = "Test.Rendering";
        EXPECT_EQ(registry.RegisterSection(std::move(bad_section)).status,
                  SettingsResultStatus::InvalidDefinition);

        auto bad_property = MakeSection();
        bad_property.properties.front().default_value = ReflectionValue{"true"};
        EXPECT_EQ(registry.RegisterSection(std::move(bad_property)).status,
                  SettingsResultStatus::InvalidDefinition);

        auto undotted_section = MakeSection();
        undotted_section.key = "rendering";
        EXPECT_EQ(registry.RegisterSection(std::move(undotted_section)).status,
                  SettingsResultStatus::InvalidDefinition);
    }

    TEST(SettingsRegistryTest, RejectsInvalidNumericMetadataAndOutOfRangeDefaults)
    {
        SettingsRegistry registry;
        auto reversed_range = MakeSection();
        reversed_range.properties[1].metadata.minimum = 2.0;
        reversed_range.properties[1].metadata.maximum = 1.0;
        EXPECT_EQ(registry.RegisterSection(std::move(reversed_range)).status,
                  SettingsResultStatus::InvalidDefinition);

        auto out_of_range_default = MakeSection();
        out_of_range_default.properties[1].default_value = ReflectionValue{1.5};
        EXPECT_EQ(registry.RegisterSection(std::move(out_of_range_default)).status,
                  SettingsResultStatus::InvalidDefinition);

        auto non_finite_default = MakeSection();
        non_finite_default.properties[1].default_value =
            ReflectionValue{std::numeric_limits<double>::infinity()};
        EXPECT_EQ(registry.RegisterSection(std::move(non_finite_default)).status,
                  SettingsResultStatus::InvalidDefinition);
    }

    TEST(SettingsRegistryTest, ValidatesEnumDefaultsAndOptionUniqueness)
    {
        SettingsRegistry registry;
        auto section = MakeSection();
        auto &property = section.properties[0];
        property.value_type = ReflectionValueType::SignedInteger;
        property.default_value = ReflectionValue{1};
        property.metadata.semantic = ReflectionWidgetSemantic::Enum;
        property.metadata.enum_options = {{1, "On"}, {0, "Off"}};
        EXPECT_TRUE(registry.RegisterSection(section));

        SettingsRegistry invalid_registry;
        section.key = "test.invalid_enum";
        section.properties[0].metadata.enum_options.push_back(ReflectionEnumOption{1, "Again"});
        EXPECT_EQ(invalid_registry.RegisterSection(std::move(section)).status,
                  SettingsResultStatus::InvalidDefinition);
    }

    TEST(SettingsRegistryTest, RejectsUnknownPolicyAndScopeValues)
    {
        SettingsRegistry registry;
        auto invalid_policy = MakeSection();
        invalid_policy.apply_policy = static_cast<SettingApplyPolicy>(255);
        EXPECT_EQ(registry.RegisterSection(std::move(invalid_policy)).status,
                  SettingsResultStatus::InvalidDefinition);

        auto invalid_scope = MakeSection();
        invalid_scope.allowed_save_scopes.front() = static_cast<SettingSaveScope>(255);
        EXPECT_EQ(registry.RegisterSection(std::move(invalid_scope)).status,
                  SettingsResultStatus::InvalidDefinition);
    }

    TEST(SettingsRegistryTest, RunsPureSectionValidatorAgainstDefaults)
    {
        SettingsRegistry registry;
        auto section = MakeSection();
        section.validate = [](const SettingValues &values)
        {
            const bool *enabled = values.at("enabled").TryGet<bool>();
            return enabled != nullptr && *enabled
                       ? SettingValidationResult{}
                       : SettingValidationResult::Reject("default must enable the feature");
        };
        EXPECT_TRUE(registry.RegisterSection(std::move(section)));

        SettingsRegistry invalid_registry;
        auto invalid = MakeSection();
        invalid.properties.front().default_value = ReflectionValue{false};
        invalid.validate = [](const SettingValues &)
        {
            return SettingValidationResult::Reject("cross-property invariant failed");
        };
        const SettingsResult result = invalid_registry.RegisterSection(std::move(invalid));
        EXPECT_EQ(result.status, SettingsResultStatus::InvalidDefinition);
        EXPECT_EQ(result.diagnostic, "cross-property invariant failed");
    }

    TEST(SettingsRegistryTest, SealingPreventsFurtherSchemaRegistration)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        EXPECT_TRUE(registry.IsSealed());
        EXPECT_EQ(registry.RegisterSection(MakeSection()).status,
                  SettingsResultStatus::Sealed);
        EXPECT_EQ(registry.GetSections().size(), 1u);
    }

    TEST(SettingsRegistryTest, ResolvesLayersInPriorityOrderWithPerPropertyProvenance)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());

        ASSERT_TRUE(registry.SetPersistentOverrides(
            SettingSaveScope::Project,
            {{"test.rendering", {{"intensity", ReflectionValue{0.8}}}}}));
        ASSERT_TRUE(registry.SetPersistentOverrides(
            SettingSaveScope::User,
            {{"test.rendering", {{"enabled", ReflectionValue{false}},
                                 {"intensity", ReflectionValue{0.7}}}}}));
        ASSERT_TRUE(registry.SetSessionOverrides(
            {{"test.rendering", {{"intensity", ReflectionValue{0.2}}}}}));

        auto snapshot = registry.GetResolvedSnapshot();
        ASSERT_EQ(snapshot.sections.size(), 1u);
        const auto &properties = snapshot.sections.front().properties;
        EXPECT_EQ(properties.at("enabled").value, (ReflectionValue{false}));
        EXPECT_EQ(properties.at("enabled").source, SettingValueSource::User);
        EXPECT_EQ(properties.at("intensity").value, (ReflectionValue{0.2}));
        EXPECT_EQ(properties.at("intensity").source, SettingValueSource::Session);

        ASSERT_TRUE(registry.RemoveSessionOverride("test.rendering", "intensity"));
        snapshot = registry.GetResolvedSnapshot();
        EXPECT_EQ(snapshot.sections.front().properties.at("intensity").value,
                  (ReflectionValue{0.7}));
        EXPECT_EQ(snapshot.sections.front().properties.at("intensity").source,
                  SettingValueSource::User);

        ASSERT_TRUE(registry.RemovePersistentOverride(
            SettingSaveScope::User, "test.rendering", "intensity"));
        snapshot = registry.GetResolvedSnapshot();
        EXPECT_EQ(snapshot.sections.front().properties.at("intensity").value,
                  (ReflectionValue{0.8}));
        EXPECT_EQ(snapshot.sections.front().properties.at("intensity").source,
                  SettingValueSource::Project);

        ASSERT_TRUE(registry.RemovePersistentOverride(
            SettingSaveScope::Project, "test.rendering", "intensity"));
        snapshot = registry.GetResolvedSnapshot();
        EXPECT_EQ(snapshot.sections.front().properties.at("intensity").value,
                  (ReflectionValue{0.5}));
        EXPECT_EQ(snapshot.sections.front().properties.at("intensity").source,
                  SettingValueSource::CompiledDefault);
    }

    TEST(SettingsRegistryTest, InvalidLayerFallsBackAsAWholeToTheLowerLayer)
    {
        SettingsRegistry registry;
        auto section = MakeSection();
        section.validate = [](const SettingValues &values)
        {
            const bool *enabled = values.at("enabled").TryGet<bool>();
            const double *intensity = values.at("intensity").TryGet<double>();
            if (enabled != nullptr && !*enabled && intensity != nullptr && *intensity > 0.5)
            {
                return SettingValidationResult::Reject(
                    "disabled rendering cannot use high intensity");
            }
            return SettingValidationResult{};
        };
        ASSERT_TRUE(registry.RegisterSection(std::move(section)));
        ASSERT_TRUE(registry.Seal());
        ASSERT_TRUE(registry.SetPersistentOverrides(
            SettingSaveScope::Project,
            {{"test.rendering", {{"intensity", ReflectionValue{0.8}}}}}));
        ASSERT_TRUE(registry.SetPersistentOverrides(
            SettingSaveScope::User,
            {{"test.rendering", {{"enabled", ReflectionValue{false}}}}}));

        const auto snapshot = registry.GetResolvedSnapshot();
        ASSERT_EQ(snapshot.sections.size(), 1u);
        const auto &properties = snapshot.sections.front().properties;
        EXPECT_EQ(properties.at("enabled").value, (ReflectionValue{true}));
        EXPECT_EQ(properties.at("enabled").source, SettingValueSource::CompiledDefault);
        EXPECT_EQ(properties.at("intensity").value, (ReflectionValue{0.8}));
        EXPECT_EQ(properties.at("intensity").source, SettingValueSource::Project);
        ASSERT_EQ(snapshot.diagnostics.size(), 1u);
        EXPECT_EQ(snapshot.diagnostics.front().source, SettingValueSource::User);
    }

    TEST(SettingsRegistryTest, EnforcesAllowedPersistentScopesButAllowsSessionOverrides)
    {
        SettingsRegistry registry;
        auto section = MakeSection();
        section.allowed_save_scopes = {SettingSaveScope::Project};
        ASSERT_TRUE(registry.RegisterSection(std::move(section)));
        ASSERT_TRUE(registry.Seal());

        EXPECT_EQ(registry.SetPersistentOverrides(
                      SettingSaveScope::User,
                      {{"test.rendering", {{"enabled", ReflectionValue{false}}}}})
                      .status,
                  SettingsResultStatus::InvalidValues);
        ASSERT_TRUE(registry.SetSessionOverrides(
            {{"test.rendering", {{"enabled", ReflectionValue{false}}}}}));
        const auto snapshot = registry.GetResolvedSnapshot();
        EXPECT_EQ(snapshot.sections.front().properties.at("enabled").value,
                  (ReflectionValue{false}));
        EXPECT_EQ(snapshot.sections.front().properties.at("enabled").source,
                  SettingValueSource::Session);
    }

    TEST(SettingsRegistryTest, ReportsInvalidLayerValuesWithoutPartiallyApplyingThem)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        ASSERT_TRUE(registry.SetPersistentOverrides(
            SettingSaveScope::Project,
            {{"test.rendering", {{"enabled", ReflectionValue{false}},
                                 {"unknown", ReflectionValue{1}}}}}));

        const auto snapshot = registry.GetResolvedSnapshot();
        EXPECT_EQ(snapshot.sections.front().properties.at("enabled").value,
                  (ReflectionValue{true}));
        EXPECT_EQ(snapshot.sections.front().properties.at("enabled").source,
                  SettingValueSource::CompiledDefault);
        ASSERT_EQ(snapshot.diagnostics.size(), 1u);
        EXPECT_EQ(snapshot.diagnostics.front().source, SettingValueSource::Project);
    }

    TEST(SettingsProviderTest, AppliesOnlyAfterOwnerLanePumpAndAcknowledgement)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        FakeSettingsProvider provider;
        provider.on_apply = [&registry]
        {
            EXPECT_EQ(registry.GetResolvedSnapshot().sections.size(), 1u);
        };
        auto binding = registry.BindProvider("test.rendering", provider);
        ASSERT_TRUE(binding);
        EXPECT_TRUE(binding.registration.IsActive());

        const auto before = registry.GetResolvedSnapshot();
        const auto edit = registry.SubmitEdit(
            "test.rendering", before.sections.front().revision,
            {{"intensity", ReflectionValue{0.8}}}, SettingsEditOrigin::Editor);
        ASSERT_EQ(edit.status, SettingsEditStatus::Pending);
        EXPECT_EQ(provider.apply_count, 0u);
        EXPECT_EQ(registry.SubmitEdit(
                      "test.rendering", before.sections.front().revision,
                      {{"intensity", ReflectionValue{0.7}}}, SettingsEditOrigin::Command)
                      .status,
                  SettingsEditStatus::Busy);

        ASSERT_EQ(registry.PumpProviders(), 1u);
        EXPECT_EQ(provider.apply_count, 1u);
        EXPECT_EQ(provider.last_request.changed_values.at("intensity"),
                  (ReflectionValue{0.8}));
        EXPECT_EQ(registry.GetEditResult(edit.request_id)->status,
                  SettingsEditStatus::Pending);

        ASSERT_TRUE(provider.Complete(SettingsProviderOutcome::Applied,
                                      provider.last_request.candidate_values));
        const auto applied = registry.GetEditResult(edit.request_id);
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(applied->status, SettingsEditStatus::Applied);
        EXPECT_EQ(applied->active_values.at("intensity"), (ReflectionValue{0.8}));
        const auto after = registry.GetResolvedSnapshot();
        EXPECT_EQ(after.sections.front().properties.at("intensity").value,
                  (ReflectionValue{0.8}));
        EXPECT_EQ(after.sections.front().properties.at("intensity").source,
                  SettingValueSource::Session);
        EXPECT_FALSE(provider.Complete(SettingsProviderOutcome::Applied,
                                       provider.last_request.candidate_values));
    }

    TEST(SettingsProviderTest, RejectedAndInvalidProviderResultsDoNotChangeDesiredValues)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        FakeSettingsProvider provider;
        auto binding = registry.BindProvider("test.rendering", provider);
        ASSERT_TRUE(binding);

        const auto before = registry.GetResolvedSnapshot();
        const auto edit = registry.SubmitEdit(
            "test.rendering", before.sections.front().revision,
            {{"intensity", ReflectionValue{0.8}}}, SettingsEditOrigin::Command);
        ASSERT_EQ(edit.status, SettingsEditStatus::Pending);
        ASSERT_EQ(registry.PumpProviders(), 1u);
        ASSERT_TRUE(provider.Complete(SettingsProviderOutcome::Rejected, {}, "backend rejected"));
        const auto rejected = registry.GetEditResult(edit.request_id);
        ASSERT_TRUE(rejected.has_value());
        EXPECT_EQ(rejected->status, SettingsEditStatus::Rejected);
        EXPECT_EQ(registry.GetResolvedSnapshot().sections.front()
                      .properties.at("intensity").value,
                  (ReflectionValue{0.5}));

        const auto next = registry.GetResolvedSnapshot();
        const auto invalid_edit = registry.SubmitEdit(
            "test.rendering", next.sections.front().revision,
            {{"intensity", ReflectionValue{0.9}}}, SettingsEditOrigin::Editor);
        ASSERT_EQ(invalid_edit.status, SettingsEditStatus::Pending);
        ASSERT_EQ(registry.PumpProviders(), 1u);
        EXPECT_TRUE(provider.Complete(
            SettingsProviderOutcome::Applied,
            {{"enabled", ReflectionValue{true}}, {"intensity", ReflectionValue{2.0}}}));
        EXPECT_EQ(registry.GetEditResult(invalid_edit.request_id)->status,
                  SettingsEditStatus::Rejected);
        EXPECT_EQ(registry.GetResolvedSnapshot().sections.front()
                      .properties.at("intensity").value,
                  (ReflectionValue{0.5}));
    }

    TEST(SettingsProviderTest, RejectsStaleDraftsAndMissingProviders)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        const auto initial = registry.GetResolvedSnapshot();
        EXPECT_EQ(registry.SubmitEdit(
                      "test.rendering", initial.sections.front().revision,
                      {{"intensity", ReflectionValue{0.8}}}, SettingsEditOrigin::Editor)
                      .status,
                  SettingsEditStatus::ProviderUnavailable);
        ASSERT_TRUE(registry.SetPersistentOverrides(
            SettingSaveScope::Project,
            {{"test.rendering", {{"intensity", ReflectionValue{0.7}}}}}));
        EXPECT_EQ(registry.SubmitEdit(
                      "test.rendering", initial.sections.front().revision,
                      {{"intensity", ReflectionValue{0.8}}}, SettingsEditOrigin::Editor)
                      .status,
                  SettingsEditStatus::StaleRevision);
    }

    TEST(SettingsProviderTest, UnregisterCancelsPendingWorkAndIgnoresLateAcknowledgements)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        FakeSettingsProvider provider;
        auto binding = registry.BindProvider("test.rendering", provider);
        ASSERT_TRUE(binding);
        const uint64_t first_generation = binding.registration.Generation();

        const auto snapshot = registry.GetResolvedSnapshot();
        const auto edit = registry.SubmitEdit(
            "test.rendering", snapshot.sections.front().revision,
            {{"intensity", ReflectionValue{0.8}}}, SettingsEditOrigin::Command);
        ASSERT_EQ(edit.status, SettingsEditStatus::Pending);
        ASSERT_EQ(registry.PumpProviders(), 1u);
        const SettingsCompletionHandle late_completion = provider.last_completion;

        binding.registration.Reset();
        EXPECT_EQ(registry.GetEditResult(edit.request_id)->status,
                  SettingsEditStatus::Cancelled);
        EXPECT_FALSE(late_completion.Complete({SettingsProviderOutcome::Applied,
                                               provider.last_request.candidate_values, {}}));

        auto rebound = registry.BindProvider("test.rendering", provider);
        ASSERT_TRUE(rebound);
        EXPECT_GT(rebound.registration.Generation(), first_generation);
    }

    TEST(SettingsProviderTest, LateCompletionIsSafeAfterRegistryShutdown)
    {
        auto registry = std::make_unique<SettingsRegistry>();
        ASSERT_TRUE(registry->RegisterSection(MakeSection()));
        ASSERT_TRUE(registry->Seal());
        FakeSettingsProvider provider;
        auto binding = registry->BindProvider("test.rendering", provider);
        ASSERT_TRUE(binding);
        const auto snapshot = registry->GetResolvedSnapshot();
        const auto edit = registry->SubmitEdit(
            "test.rendering", snapshot.sections.front().revision,
            {{"intensity", ReflectionValue{0.8}}}, SettingsEditOrigin::Internal);
        ASSERT_EQ(edit.status, SettingsEditStatus::Pending);
        ASSERT_EQ(registry->PumpProviders(), 1u);
        const SettingsCompletionHandle late_completion = provider.last_completion;

        registry.reset();
        EXPECT_FALSE(late_completion.Complete({SettingsProviderOutcome::Applied,
                                               provider.last_request.candidate_values, {}}));
    }

    TEST(SettingsProviderTest, RestartRequiredEditUpdatesSessionIntentWithoutProvider)
    {
        SettingsRegistry registry;
        auto section = MakeSection();
        section.apply_policy = SettingApplyPolicy::RestartRequired;
        ASSERT_TRUE(registry.RegisterSection(std::move(section)));
        ASSERT_TRUE(registry.Seal());
        const auto snapshot = registry.GetResolvedSnapshot();

        const auto result = registry.SubmitEdit(
            "test.rendering", snapshot.sections.front().revision,
            {{"enabled", ReflectionValue{false}}}, SettingsEditOrigin::CommandLine);
        EXPECT_EQ(result.status, SettingsEditStatus::RestartRequired);
        EXPECT_EQ(registry.GetResolvedSnapshot().sections.front()
                      .properties.at("enabled").value,
                  (ReflectionValue{false}));
        EXPECT_EQ(registry.GetResolvedSnapshot().sections.front()
                      .properties.at("enabled").source,
                  SettingValueSource::Session);
    }

    TEST(SettingsProviderTest, BoundsTheProviderQueue)
    {
        constexpr std::size_t maximum_queued_requests = 64;
        SettingsRegistry registry;
        std::vector<std::string> section_keys;
        section_keys.reserve(maximum_queued_requests + 1);
        for (std::size_t index = 0; index <= maximum_queued_requests; ++index)
        {
            auto section = MakeSection();
            section.key = "test.queue_" + std::to_string(index);
            section_keys.push_back(section.key);
            ASSERT_TRUE(registry.RegisterSection(std::move(section)));
        }
        ASSERT_TRUE(registry.Seal());

        std::vector<std::unique_ptr<FakeSettingsProvider>> providers;
        std::vector<SettingsProviderRegistration> bindings;
        providers.reserve(section_keys.size());
        bindings.reserve(section_keys.size());
        for (const std::string &section_key : section_keys)
        {
            providers.push_back(std::make_unique<FakeSettingsProvider>());
            auto binding = registry.BindProvider(section_key, *providers.back());
            ASSERT_TRUE(binding);
            bindings.push_back(std::move(binding.registration));
        }

        for (std::size_t index = 0; index < maximum_queued_requests; ++index)
        {
            const auto result = registry.SubmitEdit(
                section_keys[index], 0, {{"enabled", ReflectionValue{false}}},
                SettingsEditOrigin::Internal);
            ASSERT_EQ(result.status, SettingsEditStatus::Pending) << index;
        }
        const auto overflow = registry.SubmitEdit(
            section_keys.back(), 0, {{"enabled", ReflectionValue{false}}},
            SettingsEditOrigin::Internal);
        EXPECT_EQ(overflow.status, SettingsEditStatus::Busy);
    }

    TEST(SettingsProviderTest, BoundsRetainedCompletionHistory)
    {
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        FakeSettingsProvider provider;
        provider.auto_apply = true;
        auto binding = registry.BindProvider("test.rendering", provider);
        ASSERT_TRUE(binding);

        uint64_t first_request_id = 0;
        uint64_t last_request_id = 0;
        for (uint64_t index = 0; index < 129; ++index)
        {
            const auto snapshot = registry.GetResolvedSnapshot();
            const auto edit = registry.SubmitEdit(
                "test.rendering", snapshot.sections.front().revision,
                {{"enabled", ReflectionValue{(index % 2) != 0}}},
                SettingsEditOrigin::Internal);
            ASSERT_EQ(edit.status, SettingsEditStatus::Pending) << index;
            ASSERT_EQ(registry.PumpProviders(), 1u);
            ASSERT_EQ(registry.GetEditResult(edit.request_id)->status,
                      SettingsEditStatus::Applied);
            if (index == 0)
            {
                first_request_id = edit.request_id;
            }
            last_request_id = edit.request_id;
        }

        EXPECT_FALSE(registry.GetEditResult(first_request_id).has_value());
        EXPECT_TRUE(registry.GetEditResult(last_request_id).has_value());
    }

    TEST(SettingsStoreTest, MissingFileLoadsDefaultsAndExplicitSaveRoundTripsOverrides)
    {
        TemporaryDirectory directory;
        const auto path = directory.Path() / "project_settings.json";
        SettingsRegistry registry;
        RegisterStoreSchema(registry);

        SettingsStore store(path, SettingSaveScope::Project);
        EXPECT_EQ(store.Load(registry).status, SettingsStoreStatus::MissingFile);
        ASSERT_TRUE(store.GetSnapshot().writable);

        SettingsLayerOverrides overrides{
            {"render.quality",
             {{"enabled", ReflectionValue{false}}, {"samples", ReflectionValue{4}}}}};
        const SettingsStoreResult save_result = store.Save(registry, overrides);
        ASSERT_TRUE(save_result) << save_result.diagnostic;

        SettingsStore reopened(path, SettingSaveScope::Project);
        ASSERT_TRUE(reopened.Load(registry));
        const auto snapshot = reopened.GetSnapshot();
        ASSERT_EQ(snapshot.overrides.size(), 1u);
        EXPECT_EQ(snapshot.overrides.at("render.quality").at("enabled"),
                  (ReflectionValue{false}));
        EXPECT_EQ(snapshot.overrides.at("render.quality").at("samples"),
                  (ReflectionValue{4}));
    }

    TEST(SettingsStoreTest, PreservesUnknownSectionsAndPropertiesWhenSaving)
    {
        TemporaryDirectory directory;
        const auto path = directory.Path() / "settings.json";
        const Json initial{
            {"format_version", 1},
            {"future_root", {{"keep", true}}},
            {"sections",
             {{"future.audio", {{"schema_version", 3}, {"values", {{"gain", 0.7}}}}},
              {"render.quality",
               {{"schema_version", 1},
                {"values", {{"enabled", true}, {"future_property", "preserve"}}}}}}}};
        WriteText(path, initial.dump(2));
        SettingsRegistry registry;
        RegisterStoreSchema(registry);

        SettingsStore store(path, SettingSaveScope::Project);
        ASSERT_TRUE(store.Load(registry));
        SettingsLayerOverrides overrides{
            {"render.quality",
             {{"enabled", ReflectionValue{true}}, {"samples", ReflectionValue{8}}}}};
        const SettingsStoreResult save_result = store.Save(registry, overrides);
        ASSERT_TRUE(save_result) << save_result.diagnostic;

        const Json saved = Json::parse(ReadText(path));
        EXPECT_EQ(saved.at("future_root"), initial.at("future_root"));
        EXPECT_EQ(saved.at("sections").at("future.audio"),
                  initial.at("sections").at("future.audio"));
        EXPECT_EQ(saved.at("sections").at("render.quality").at("values").at("future_property"),
                  "preserve");
        EXPECT_EQ(saved.at("sections").at("render.quality").at("values").at("samples"), 8);
    }

    TEST(SettingsStoreTest, RejectsMalformedAndFutureFormatWithoutOverwriting)
    {
        TemporaryDirectory directory;
        SettingsRegistry registry;
        RegisterStoreSchema(registry);

        const auto malformed_path = directory.Path() / "malformed.json";
        const std::string malformed = "{broken";
        WriteText(malformed_path, malformed);
        SettingsStore malformed_store(malformed_path, SettingSaveScope::Project);
        EXPECT_EQ(malformed_store.Load(registry).status,
                  SettingsStoreStatus::InvalidDocument);
        EXPECT_EQ(malformed_store.Save(registry, {}).status,
                  SettingsStoreStatus::ReadOnly);
        EXPECT_EQ(ReadText(malformed_path), malformed);

        const auto future_path = directory.Path() / "future.json";
        const std::string future =
            R"({"format_version":99,"sections":{},"future_data":true})";
        WriteText(future_path, future);
        SettingsStore future_store(future_path, SettingSaveScope::User);
        EXPECT_EQ(future_store.Load(registry).status,
                  SettingsStoreStatus::UnsupportedVersion);
        EXPECT_EQ(future_store.Save(registry, {}).status, SettingsStoreStatus::ReadOnly);
        EXPECT_EQ(ReadText(future_path), future);
    }

    TEST(SettingsStoreTest, InvalidKnownSectionFallsBackAsAWholeAndBlocksSave)
    {
        TemporaryDirectory directory;
        const auto path = directory.Path() / "settings.json";
        const std::string contents =
            R"({"format_version":1,"sections":{"render.quality":{"schema_version":1,"values":{"enabled":false,"samples":8}}}})";
        WriteText(path, contents);
        SettingsRegistry registry;
        RegisterStoreSchema(registry);

        SettingsStore store(path, SettingSaveScope::Project);
        ASSERT_TRUE(store.Load(registry));
        const auto snapshot = store.GetSnapshot();
        EXPECT_TRUE(snapshot.overrides.empty());
        EXPECT_FALSE(snapshot.writable);
        ASSERT_EQ(snapshot.diagnostics.size(), 1u);
        EXPECT_EQ(snapshot.diagnostics.front().section_key, "render.quality");
        EXPECT_EQ(store.Save(registry, {}).status, SettingsStoreStatus::ReadOnly);
        EXPECT_EQ(ReadText(path), contents);
    }

    TEST(SettingsStoreTest, AppliesDomainMigrationToCopiedValues)
    {
        TemporaryDirectory directory;
        const auto path = directory.Path() / "settings.json";
        WriteText(path,
                  R"({"format_version":1,"sections":{"render.quality":{"schema_version":1,"values":{"enabled":true,"quality":8}}}})");
        SettingsRegistry registry;
        RegisterStoreSchema(
            registry, 2,
            [](uint32_t from_version, const SettingValues &old_values)
            {
                if (from_version != 1)
                {
                    return SettingMigrationResult::Reject("unsupported source schema");
                }
                SettingValues migrated{{"enabled", old_values.at("enabled")},
                                        {"samples", old_values.at("quality")}};
                return SettingMigrationResult{true, std::move(migrated), {"quality"}, {}};
            });

        SettingsStore store(path, SettingSaveScope::Project);
        ASSERT_TRUE(store.Load(registry));
        const auto snapshot = store.GetSnapshot();
        ASSERT_TRUE(snapshot.writable)
            << (snapshot.diagnostics.empty() ? "" : snapshot.diagnostics.front().message);
        ASSERT_EQ(snapshot.overrides.size(), 1u);
        EXPECT_EQ(snapshot.overrides.at("render.quality").at("samples"),
                  (ReflectionValue{8}));
        EXPECT_EQ(Json::parse(ReadText(path)).at("sections").at("render.quality")
                      .at("schema_version"),
                  1);

        ASSERT_TRUE(store.Save(registry, snapshot.overrides));
        const Json saved = Json::parse(ReadText(path));
        EXPECT_EQ(saved.at("sections").at("render.quality").at("schema_version"), 2);
        EXPECT_FALSE(saved.at("sections").at("render.quality").at("values")
                         .contains("quality"));
    }

    TEST(SettingsStoreTest, RefusesSavingWhenDiskChangedAfterLoad)
    {
        TemporaryDirectory directory;
        const auto path = directory.Path() / "settings.json";
        WriteText(path, R"({"format_version":1,"sections":{}})");
        SettingsRegistry registry;
        RegisterStoreSchema(registry);
        SettingsStore store(path, SettingSaveScope::Project);
        ASSERT_TRUE(store.Load(registry));

        const std::string external_edit =
            R"({"format_version":1,"sections":{},"external":true})";
        WriteText(path, external_edit);
        EXPECT_EQ(store.Save(registry, {}).status, SettingsStoreStatus::DiskConflict);
        EXPECT_EQ(ReadText(path), external_edit);
    }

    TEST(SettingsStoreTest, RejectsValuesOutsideTheSectionSchema)
    {
        TemporaryDirectory directory;
        SettingsRegistry registry;
        RegisterStoreSchema(registry);
        SettingsStore store(directory.Path() / "settings.json", SettingSaveScope::User);
        ASSERT_TRUE(store.Load(registry));

        EXPECT_EQ(store.Save(registry,
                             {{"render.quality", {{"unknown", ReflectionValue{1}}}}})
                      .status,
                  SettingsStoreStatus::InvalidValues);
        EXPECT_FALSE(std::filesystem::exists(directory.Path() / "settings.json"));
    }

    TEST(SettingsEditorAccessTest, SaveRunsOnlyAfterRuntimePumpAndUpdatesRegistryAfterSuccess)
    {
        TemporaryDirectory directory;
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        SettingsStore project_store(directory.Path() / "project.json",
                                    SettingSaveScope::Project);
        SettingsStore user_store(directory.Path() / "user.json", SettingSaveScope::User);
        ASSERT_TRUE(project_store.Load(registry));
        ASSERT_TRUE(user_store.Load(registry));
        RuntimeSettingsEditorAccess access(registry, project_store, user_store);

        const auto save = access.SubmitSave(SettingSaveScope::Project, "test.rendering");
        ASSERT_EQ(save.status, SettingsSaveRequestStatus::Pending);
        EXPECT_FALSE(std::filesystem::exists(directory.Path() / "project.json"));
        EXPECT_EQ(access.GetSaveResult(save.request_id)->status,
                  SettingsSaveRequestStatus::Pending);

        EXPECT_EQ(access.PumpGameLane(), 1u);
        const auto completed = access.GetSaveResult(save.request_id);
        ASSERT_TRUE(completed.has_value());
        EXPECT_EQ(completed->status, SettingsSaveRequestStatus::Succeeded);
        EXPECT_TRUE(std::filesystem::exists(directory.Path() / "project.json"));
        EXPECT_TRUE(registry.GetPersistentOverrides(SettingSaveScope::Project)
                        .contains("test.rendering"));
    }

    TEST(SettingsEditorAccessTest, DiskConflictDoesNotUpdateRegistryOverrides)
    {
        TemporaryDirectory directory;
        SettingsRegistry registry;
        ASSERT_TRUE(registry.RegisterSection(MakeSection()));
        ASSERT_TRUE(registry.Seal());
        const auto project_path = directory.Path() / "project.json";
        SettingsStore project_store(project_path, SettingSaveScope::Project);
        SettingsStore user_store(directory.Path() / "user.json", SettingSaveScope::User);
        ASSERT_TRUE(project_store.Load(registry));
        ASSERT_TRUE(user_store.Load(registry));
        RuntimeSettingsEditorAccess access(registry, project_store, user_store);
        WriteText(project_path, "external writer");

        const auto save = access.SubmitSave(SettingSaveScope::Project, "test.rendering");
        ASSERT_EQ(save.status, SettingsSaveRequestStatus::Pending);
        EXPECT_EQ(access.PumpGameLane(), 1u);
        const auto completed = access.GetSaveResult(save.request_id);
        ASSERT_TRUE(completed.has_value());
        EXPECT_EQ(completed->status, SettingsSaveRequestStatus::Failed);
        EXPECT_TRUE(registry.GetPersistentOverrides(SettingSaveScope::Project).empty());
    }
}
