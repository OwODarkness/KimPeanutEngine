#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "command/command_registry.h"
#include "panel_command_provider.h"
#include "render/panel_render_contract.h"

namespace kpengine::panel
{
    namespace
    {
        namespace command = kpengine::runtime::command;

        // Records what the host would have been asked for. Everything else is
        // stubbed: these tests are about the commands' contract, not the panel.
        struct Spy final
        {
            int report_calls = 0;
            bool report_available = true;
            PanelRuntimeReport report{};

            int set_text_calls = 0;
            std::string last_text;
            bool set_text_succeeds = true;
            std::string set_text_diagnostic;

            int set_appearance_calls = 0;
            PanelAppearanceUpdate last_update{};
            bool set_appearance_succeeds = true;

            PanelCommandResolvers Resolvers()
            {
                PanelCommandResolvers resolvers;
                resolvers.report = [this](PanelRuntimeReport &out)
                {
                    ++report_calls;
                    if (!report_available)
                    {
                        return false;
                    }
                    out = report;
                    return true;
                };
                resolvers.set_text = [this](std::string_view text, std::string &diagnostic)
                {
                    ++set_text_calls;
                    last_text = std::string{text};
                    diagnostic = set_text_diagnostic;
                    return set_text_succeeds;
                };
                resolvers.set_appearance = [this](const PanelAppearanceUpdate &update,
                                                  std::string &)
                {
                    ++set_appearance_calls;
                    last_update = update;
                    return set_appearance_succeeds;
                };
                return resolvers;
            }
        };

        // Dispatches from the Agent origin the way the transport does: these are
        // Game-lane commands, so the request is queued and drained by
        // PumpGameThread rather than completing inline.
        command::CommandResult ExecuteAndPump(command::CommandRegistry &registry,
                                              command::CommandCall call)
        {
            const command::CommandResult pending = registry.Execute(
                call, {command::CommandOrigin::Agent, command::CommandThread::Immediate,
                       command::CommandCapability::Mutating});
            if (pending.status != command::CommandStatus::Pending)
            {
                return pending;
            }
            if (registry.PumpGameThread() != 1U)
            {
                return {command::CommandStatus::Failed, "game lane did not run", 0, {}};
            }
            const std::optional<command::CommandResult> completion =
                registry.TakeCompletion(pending.request_id);
            return completion.has_value()
                       ? *completion
                       : command::CommandResult{command::CommandStatus::Failed,
                                                "no completion recorded", 0, {}};
        }

        const std::string *GetString(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr
                                          : std::get_if<std::string>(&iterator->second);
        }

        const double *GetDouble(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr : std::get_if<double>(&iterator->second);
        }
    }

    TEST(PanelCommandProviderTest, RegistersEveryCommand)
    {
        Spy spy;
        command::CommandRegistry registry;
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());

        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;
        // One token per command; dropping any of them unregisters that command.
        EXPECT_EQ(registration.registrations.size(), 3u);

        // Each command reaches its resolver, which proves it is installed and
        // routed rather than merely described.
        EXPECT_TRUE(ExecuteAndPump(registry, {"panel.report", {}}).IsSuccess());
        EXPECT_EQ(spy.report_calls, 1);

        EXPECT_TRUE(ExecuteAndPump(registry,
                                   {"panel.set_text", {{"text", std::string{"x"}}}})
                        .IsSuccess());
        EXPECT_EQ(spy.set_text_calls, 1);

        EXPECT_TRUE(ExecuteAndPump(registry,
                                   {"panel.set_appearance", {{"dot_gap", 0.2}}})
                        .IsSuccess());
        EXPECT_EQ(spy.set_appearance_calls, 1);
    }

    TEST(PanelCommandProviderTest, RejectsAMissingResolver)
    {
        command::CommandRegistry registry;
        PanelCommandResolvers incomplete;
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, std::move(incomplete));

        EXPECT_FALSE(registration.succeeded);
        EXPECT_FALSE(registration.diagnostic.empty());
        EXPECT_TRUE(registration.registrations.empty());
    }

    TEST(PanelCommandProviderTest, ReportCarriesTheAppearanceAlongsideTheProduct)
    {
        Spy spy;
        spy.report.glyphs_loaded = true;
        spy.report.glyph_product = "panel/glyphs-16.kppnlgl";
        spy.report.dot_gap = 0.3f;
        spy.report.dot_color = "#FF6600";
        spy.report.background_color = "#000000";

        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(registry, {"panel.report", {}});
        ASSERT_TRUE(result.IsSuccess()) << result.message;

        ASSERT_NE(GetString(result.data, "glyph_product"), nullptr);
        EXPECT_EQ(*GetString(result.data, "glyph_product"), "panel/glyphs-16.kppnlgl");
        ASSERT_NE(GetDouble(result.data, "dot_gap"), nullptr);
        // Near rather than exact: the appearance is held as a float and the
        // command payload widens it to a double, so 0.3f arrives as
        // 0.30000001192 rather than 0.3.
        EXPECT_NEAR(*GetDouble(result.data, "dot_gap"), 0.3, 1.0e-6);
        ASSERT_NE(GetString(result.data, "dot_color"), nullptr);
        EXPECT_EQ(*GetString(result.data, "dot_color"), "#FF6600");
    }

    TEST(PanelCommandProviderTest, ReportFailsWhenNoPanelIsLoaded)
    {
        Spy spy;
        spy.report_available = false;

        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(registry, {"panel.report", {}});
        EXPECT_FALSE(result.IsSuccess());
        EXPECT_EQ(spy.report_calls, 1);
    }

    TEST(PanelCommandProviderTest, SetAppearanceAppliesOnlyTheFieldsGiven)
    {
        Spy spy;
        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(
            registry, {"panel.set_appearance", {{"dot_gap", 0.1}}});
        ASSERT_TRUE(result.IsSuccess()) << result.message;
        EXPECT_EQ(spy.set_appearance_calls, 1);
        EXPECT_TRUE(spy.last_update.has_dot_gap);
        EXPECT_FALSE(spy.last_update.has_dot_color);
        EXPECT_FALSE(spy.last_update.has_background_color);
        EXPECT_FLOAT_EQ(spy.last_update.dot_gap, 0.1f);
    }

    TEST(PanelCommandProviderTest, SetAppearanceRequiresAGapWrittenAsAFloat)
    {
        // The transport types a literal by its value, so `0` arrives as an
        // unsigned integer and `0.0` as a double. The schema's Float check
        // requires a double, so a whole-number gap must carry a decimal point.
        // Pinned because it is a real papercut: `{"dot_gap": 0}` is refused, and
        // the refusal has to be the *schema's*, not a silently clamped value.
        Spy spy;
        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult as_double = ExecuteAndPump(
            registry, {"panel.set_appearance", {{"dot_gap", 0.0}}});
        ASSERT_TRUE(as_double.IsSuccess()) << as_double.message;
        EXPECT_TRUE(spy.last_update.has_dot_gap);
        EXPECT_FLOAT_EQ(spy.last_update.dot_gap, 0.0f);

        const command::CommandResult as_integer = ExecuteAndPump(
            registry, {"panel.set_appearance", {{"dot_gap", std::uint64_t{0}}}});
        EXPECT_EQ(as_integer.status, command::CommandStatus::InvalidArguments);
        EXPECT_NE(as_integer.message.find("dot_gap"), std::string::npos);
    }

    TEST(PanelCommandProviderTest, SetAppearanceParsesHexColours)
    {
        Spy spy;
        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(
            registry, {"panel.set_appearance",
                       {{"dot_color", std::string{"#FF8000"}},
                        {"background_color", std::string{"#102030"}}}});
        ASSERT_TRUE(result.IsSuccess()) << result.message;
        EXPECT_TRUE(spy.last_update.has_dot_color);
        EXPECT_TRUE(spy.last_update.has_background_color);
        EXPECT_FLOAT_EQ(spy.last_update.dot_color[0], 1.0f);
        EXPECT_NEAR(spy.last_update.dot_color[1], 128.0f / 255.0f, 1.0e-6f);
        EXPECT_FLOAT_EQ(spy.last_update.dot_color[2], 0.0f);
        EXPECT_FLOAT_EQ(spy.last_update.dot_color[3], 1.0f);
        EXPECT_NEAR(spy.last_update.background_color[2], 48.0f / 255.0f, 1.0e-6f);
    }

    TEST(PanelCommandProviderTest, SetAppearanceAcceptsTheRamp)
    {
        Spy spy;
        command::CommandRegistry registry;
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(
            registry, {"panel.set_appearance",
                       {{"gradient", 0.8},
                        {"gradient_axis", std::string{"mirrored"}},
                        {"accent_color", std::string{"#00E5FF"}},
                        {"cycles_per_second", 0.25}}});
        ASSERT_TRUE(result.IsSuccess()) << result.message;

        EXPECT_TRUE(spy.last_update.has_gradient_amount);
        EXPECT_FLOAT_EQ(spy.last_update.gradient_amount, 0.8f);
        EXPECT_TRUE(spy.last_update.has_gradient_axis);
        EXPECT_EQ(spy.last_update.gradient_axis,
                  static_cast<std::uint32_t>(PanelGradientAxis::Mirrored));
        EXPECT_TRUE(spy.last_update.has_accent_color);
        EXPECT_NEAR(spy.last_update.accent_color[1], 229.0f / 255.0f, 1.0e-6f);
        EXPECT_TRUE(spy.last_update.has_cycles_per_second);
        EXPECT_FLOAT_EQ(spy.last_update.cycles_per_second, 0.25f);
    }

    TEST(PanelCommandProviderTest, EveryAxisTheReportSpellsIsOneTheCommandAccepts)
    {
        // A round trip through set_appearance and report has to compare equal, so
        // the formatter and the accepted names must come from one list.
        for (const std::uint32_t axis :
             {static_cast<std::uint32_t>(PanelGradientAxis::Horizontal),
              static_cast<std::uint32_t>(PanelGradientAxis::Vertical),
              static_cast<std::uint32_t>(PanelGradientAxis::Mirrored)})
        {
            const std::string name = FormatPanelGradientAxis(axis);
            EXPECT_NE(name, "unknown");

            Spy spy;
            command::CommandRegistry registry;
            const PanelCommandRegistrationResult registration =
                RegisterPanelCommands(registry, spy.Resolvers());
            ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

            const command::CommandResult result =
                ExecuteAndPump(registry,
                               {"panel.set_appearance", {{"gradient_axis", name}}});
            EXPECT_TRUE(result.IsSuccess()) << name << ": " << result.message;
            EXPECT_EQ(spy.last_update.gradient_axis, axis);
        }

        EXPECT_EQ(FormatPanelGradientAxis(99u), "unknown");
    }

    TEST(PanelCommandProviderTest, SetAppearanceRejectsAnUnusableValue)
    {
        Spy spy;
        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const auto rejected = [&](const command::CommandArguments &arguments)
        {
            const command::CommandResult result =
                ExecuteAndPump(registry, {"panel.set_appearance", arguments});
            EXPECT_EQ(result.status, command::CommandStatus::InvalidArguments);
            EXPECT_EQ(spy.set_appearance_calls, 0) << "an unusable value reached the host";
        };

        // A gap the shader would clamp is rejected here rather than reported as
        // applied, so a success never means something other than what was asked.
        rejected({{"dot_gap", 0.9}});
        rejected({{"dot_gap", -0.1}});
        // Not a colour.
        rejected({{"dot_color", std::string{"not a color"}}});
        rejected({{"dot_color", std::string{"#GGGGGG"}}});
        rejected({{"dot_color", std::string{"#FFF"}}});
        rejected({{"background_color", std::string{"FF6600"}}});
        // Not a number.
        rejected({{"dot_gap", std::string{"0.25"}}});
        // A ramp value outside the range, and an axis that is not one of the
        // three names.
        rejected({{"gradient", 1.5}});
        rejected({{"gradient", -0.1}});
        rejected({{"gradient_axis", std::string{"diagonal"}}});
        rejected({{"gradient_axis", std::string{""}}});
        // Far past a pleasant shimmer, so a mistake rather than a preference.
        rejected({{"cycles_per_second", 99.0}});
        // Nothing to do.
        rejected({});
    }

    TEST(PanelCommandProviderTest, SetTextForwardsTheStringAndItsFailure)
    {
        Spy spy;
        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult applied =
            ExecuteAndPump(registry, {"panel.set_text", {{"text", std::string{"OvO"}}}});
        ASSERT_TRUE(applied.IsSuccess()) << applied.message;
        EXPECT_EQ(spy.last_text, "OvO");

        spy.set_text_succeeds = false;
        spy.set_text_diagnostic = "no glyph product is loaded";
        const command::CommandResult refused =
            ExecuteAndPump(registry, {"panel.set_text", {{"text", std::string{"OvO"}}}});
        EXPECT_FALSE(refused.IsSuccess());
        EXPECT_EQ(refused.message, "no glyph product is loaded");
    }

    TEST(PanelCommandProviderTest, SetTextRequiresAString)
    {
        Spy spy;
        command::CommandRegistry registry;
        // The result owns the tokens that keep the entries installed: dropping it
        // unregisters every command, and a dispatch then reports NotFound rather
        // than reaching the resolver. Verified the hard way.
        const PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, spy.Resolvers());
        ASSERT_TRUE(registration.succeeded) << registration.diagnostic;

        const command::CommandResult missing =
            ExecuteAndPump(registry, {"panel.set_text", {}});
        EXPECT_EQ(missing.status, command::CommandStatus::InvalidArguments);

        const command::CommandResult wrong_type =
            ExecuteAndPump(registry, {"panel.set_text", {{"text", std::uint64_t{7}}}});
        EXPECT_EQ(wrong_type.status, command::CommandStatus::InvalidArguments);
        EXPECT_EQ(spy.set_text_calls, 0);
    }

    TEST(PanelCommandProviderTest, SpellsAColourTheSameWayTheCommandParsesIt)
    {
        EXPECT_EQ(FormatPanelColor({1.0f, 128.0f / 255.0f, 0.0f, 1.0f}), "#FF8000");
        EXPECT_EQ(FormatPanelColor({0.0f, 0.0f, 0.0f, 1.0f}), "#000000");
        EXPECT_EQ(FormatPanelColor({1.0f, 1.0f, 1.0f, 1.0f}), "#FFFFFF");
    }
}
