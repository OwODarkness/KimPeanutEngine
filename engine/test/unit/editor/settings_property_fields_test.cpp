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
}
