#include "settings_property_fields_view.h"
#include "editor_ui/ui_recording_backend.h"
#include "editor_ui/ui_session.h"

#include <gtest/gtest.h>

#include <memory>

namespace kpengine::editor
{
    TEST(SettingsPropertyFieldsTest, PreservesUnsignedPrecisionAndStableSemanticIdentity)
    {
        settings::SettingPropertyDefinition property{};
        property.key = "large_value";
        property.value_type = reflection::ReflectionValueType::UnsignedInteger;
        property.metadata.display_name = "Large value";
        property.metadata.minimum = 0.0;
        const reflection::ReflectionValue value{std::uint64_t{9007199254740993ULL}};
        const ui::UiNodeId id = SettingsPropertyFieldsView::StableId("render", property.key);
        ASSERT_NE(id, 0U);

        auto backend = std::make_unique<ui::UiRecordingBackend>(
            std::vector<ui::UiScriptedInput>{{id, std::uint64_t{9007199254740995ULL}}});
        auto *const recorder = backend.get();
        ui::UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "Settings"});
            const auto edit = SettingsPropertyFieldsView::Draw(
                session.Context(), "render", property, value);
            EXPECT_EQ(edit.value, ui::UiValue{std::uint64_t{9007199254740995ULL}});
        }
        ASSERT_TRUE(session.EndFrame().complete);
        const auto observations = recorder->Observations();
        ASSERT_EQ(observations.size(), 2U);
        EXPECT_EQ(observations[1].id, id);
        EXPECT_EQ(observations[1].role, ui::UiRole::NumericInput);
        EXPECT_EQ(observations[1].label, "Large value");
    }

    TEST(SettingsPropertyFieldsTest, EnumAndReadOnlyMetadataBecomeExplicitControlState)
    {
        settings::SettingPropertyDefinition property{};
        property.key = "mode";
        property.value_type = reflection::ReflectionValueType::SignedInteger;
        property.metadata.semantic = reflection::ReflectionWidgetSemantic::Enum;
        property.metadata.enum_options = {{1, "Fast"}, {2, "Quality"}};
        auto backend = std::make_unique<ui::UiRecordingBackend>();
        auto *const recorder = backend.get();
        ui::UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "Settings"});
            const auto edit = SettingsPropertyFieldsView::Draw(
                session.Context(), "render", property, reflection::ReflectionValue{1}, false);
            EXPECT_FALSE(edit.changed);
        }
        ASSERT_TRUE(session.EndFrame().complete);
        const auto observations = recorder->Observations();
        ASSERT_EQ(observations.size(), 2U);
        EXPECT_EQ(observations[1].role, ui::UiRole::Combo);
        EXPECT_FALSE(observations[1].enabled);
        EXPECT_EQ(observations[1].disabled_reason, "Read-only setting");
    }

    TEST(SettingsPropertyFieldsTest, BooleanFieldPreservesProductionObservationSemantics)
    {
        settings::SettingPropertyDefinition property{};
        property.key = "enabled";
        property.value_type = reflection::ReflectionValueType::Bool;
        property.metadata.display_name = "Neutral checkbox";

        auto backend = std::make_unique<ui::UiRecordingBackend>();
        auto *const recorder = backend.get();
        ui::UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({.observe = true}));
        {
            auto window = session.Context().Window({
                .id = ui::StableUiNodeId("smoke.window"), .label = "Editor UI Smoke"});
            const auto edit = SettingsPropertyFieldsView::Draw(
                session.Context(), "smoke", property, reflection::ReflectionValue{true});
            EXPECT_FALSE(edit.changed);
            EXPECT_TRUE(std::get<bool>(edit.value));
        }
        ASSERT_TRUE(session.EndFrame().complete);

        const auto observations = recorder->Observations();
        ASSERT_EQ(observations.size(), 2U);
        const auto &checkbox = observations.back();
        EXPECT_EQ(checkbox.id, SettingsPropertyFieldsView::StableId("smoke", "enabled"));
        EXPECT_EQ(checkbox.parent, ui::StableUiNodeId("smoke.window"));
        EXPECT_EQ(checkbox.role, ui::UiRole::CheckBox);
        EXPECT_EQ(checkbox.label, "Neutral checkbox");
        EXPECT_EQ(checkbox.value, ui::UiValue{true});
        EXPECT_TRUE(checkbox.enabled);
        EXPECT_TRUE(checkbox.disabled_reason.empty());
        ASSERT_EQ(checkbox.actions.size(), 1U);
        EXPECT_EQ(checkbox.actions.front(), ui::UiActionKind::Toggle);
        EXPECT_EQ(checkbox.geometry, ui::UiGeometryProvenance::Unavailable);
        EXPECT_FALSE(checkbox.last_action.has_value());
    }
}
