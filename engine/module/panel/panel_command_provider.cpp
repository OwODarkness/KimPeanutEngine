#include "panel_command_provider.h"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace kpengine::panel
{
    namespace
    {
        constexpr const char *kOwner = "PanelRuntime";

        const char *ResolveTextArgument(const runtime::command::CommandArguments &arguments,
                                        std::string_view &out)
        {
            const auto iterator = arguments.find("text");
            if (iterator == arguments.end())
            {
                return "panel.set_text requires a text argument";
            }
            const auto *value = std::get_if<std::string>(&iterator->second);
            if (value == nullptr)
            {
                return "panel.set_text requires text as a string";
            }
            out = *value;
            return nullptr;
        }
    }

    PanelCommandRegistrationResult RegisterPanelCommands(
        runtime::command::CommandRegistry &registry, PanelReportResolver report,
        PanelSetTextResolver set_text)
    {
        PanelCommandRegistrationResult result;
        if (!report || !set_text)
        {
            result.diagnostic = "panel command provider requires both resolvers";
            return result;
        }

        runtime::command::CommandDesc report_descriptor{
            "panel.report",
            kOwner,
            "Report the loaded panel's glyph product and dot extent",
            runtime::command::CommandCategory::Engine,
            runtime::command::CommandFlags::AgentAllowed |
                runtime::command::CommandFlags::LuaAllowed,
            {},
            [resolver = report](const runtime::command::CommandCall &call,
                                const runtime::command::CommandContext &context)
            {
                PanelRuntimeReport panel_report;
                if (!resolver(panel_report))
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::Failed,
                        "No panel is loaded in the current mode",
                        context.request_id,
                        {}};
                }

                return runtime::command::CommandResult{
                    runtime::command::CommandStatus::Success,
                    "Panel report",
                    context.request_id,
                    {{"glyph_product", panel_report.glyph_product},
                     {"glyphs_loaded", panel_report.glyphs_loaded},
                     {"first_codepoint",
                      static_cast<std::uint64_t>(panel_report.first_codepoint)},
                     {"glyph_count",
                      static_cast<std::uint64_t>(panel_report.glyph_count)},
                     {"columns", static_cast<std::uint64_t>(panel_report.columns)},
                     {"rows", static_cast<std::uint64_t>(panel_report.rows)},
                     {"dot_width", static_cast<std::uint64_t>(panel_report.dot_width)},
                     {"dot_height", static_cast<std::uint64_t>(panel_report.dot_height)},
                     {"lit_dots", static_cast<std::uint64_t>(panel_report.lit_dots)},
                     {"has_dot_mask", panel_report.has_dot_mask},
                     {"live_gpu_handles",
                      static_cast<std::uint64_t>(panel_report.live_gpu_handles)},
                     {"text", panel_report.text}}};
            },
            runtime::command::CommandThread::Game};

        // Not const: a CommandRegistration is move-only, so the token has to be
        // moved out of a mutable result rather than copied from it.
        runtime::command::CommandRegistrationResult report_registration =
            registry.Register(std::move(report_descriptor));
        if (!report_registration.IsSuccess())
        {
            result.diagnostic = report_registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(report_registration.registration));

        runtime::command::CommandDesc set_text_descriptor{
            "panel.set_text",
            kOwner,
            "Replace the panel's first row of text",
            runtime::command::CommandCategory::Engine,
            runtime::command::CommandFlags::AgentAllowed |
                runtime::command::CommandFlags::LuaAllowed |
                runtime::command::CommandFlags::MutatesState,
            {{runtime::command::CommandArgumentDesc{
                "text", runtime::command::CommandValueType::String, true, {}, {}}}},
            [resolver = std::move(set_text)](
                const runtime::command::CommandCall &call,
                const runtime::command::CommandContext &context)
            {
                std::string_view text;
                if (const char *error = ResolveTextArgument(call.arguments, text))
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::InvalidArguments, error,
                        context.request_id, {}};
                }

                std::string diagnostic;
                if (!resolver(text, diagnostic))
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::Failed, std::move(diagnostic),
                        context.request_id, {}};
                }
                return runtime::command::CommandResult{
                    runtime::command::CommandStatus::Success, "Panel text replaced",
                    context.request_id, {}};
            },
            runtime::command::CommandThread::Game};

        runtime::command::CommandRegistrationResult text_registration =
            registry.Register(std::move(set_text_descriptor));
        if (!text_registration.IsSuccess())
        {
            // The report command is already installed; unwind it so a partial
            // provider never looks like a complete one.
            result.registrations.clear();
            result.diagnostic = text_registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(text_registration.registration));

        result.succeeded = true;
        return result;
    }
}
