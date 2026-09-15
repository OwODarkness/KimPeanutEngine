#include "panel_command_provider.h"

#include <array>
#include <cstddef>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "render/panel_render_contract.h"

namespace kpengine::panel
{
    namespace
    {
        constexpr const char *kOwner = "PanelRuntime";
        // The shader clamps the gap at 0.45, so accepting more here would report
        // success for a value that renders as something else.
        constexpr double kMaximumDotGap = 0.45;
        // Ten cycles per second is already far past a pleasant shimmer, so a
        // larger value is a mistake rather than a preference.
        constexpr double kMaximumGradientCyclesPerSecond = 10.0;

        struct AxisName final
        {
            const char *name;
            std::uint32_t value;
        };

        // The names, in the enum's own numbering, so the command, the report, and
        // the shader agree without a second encoding.
        constexpr AxisName kAxisNames[] = {
            {"horizontal", static_cast<std::uint32_t>(PanelGradientAxis::Horizontal)},
            {"vertical", static_cast<std::uint32_t>(PanelGradientAxis::Vertical)},
            {"mirrored", static_cast<std::uint32_t>(PanelGradientAxis::Mirrored)}};

        runtime::command::CommandResult InvalidArguments(
            const runtime::command::CommandContext &context, std::string message)
        {
            return runtime::command::CommandResult{
                runtime::command::CommandStatus::InvalidArguments, std::move(message),
                context.request_id, {}};
        }

    }

    bool ParsePanelColor(const std::string_view text, std::array<float, 4> &out)
    {
        if (text.size() != 7u || text.front() != '#')
        {
            return false;
        }
        std::uint32_t value = 0u;
        for (std::size_t index = 1u; index < text.size(); ++index)
        {
            const char digit = text[index];
            std::uint32_t nibble = 0u;
            if (digit >= '0' && digit <= '9')
            {
                nibble = static_cast<std::uint32_t>(digit - '0');
            }
            else if (digit >= 'a' && digit <= 'f')
            {
                nibble = static_cast<std::uint32_t>(digit - 'a') + 10u;
            }
            else if (digit >= 'A' && digit <= 'F')
            {
                nibble = static_cast<std::uint32_t>(digit - 'A') + 10u;
            }
            else
            {
                return false;
            }
            value = (value << 4u) | nibble;
        }
        out = {static_cast<float>((value >> 16u) & 0xFFu) / 255.0f,
               static_cast<float>((value >> 8u) & 0xFFu) / 255.0f,
               static_cast<float>(value & 0xFFu) / 255.0f, 1.0f};
        return true;
    }

    std::string FormatPanelColor(const std::array<float, 4> &color)
    {
        const auto channel = [](const float value)
        {
            const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return static_cast<unsigned>(clamped * 255.0f + 0.5f);
        };
        char buffer[8] = {};
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", channel(color[0]),
                      channel(color[1]), channel(color[2]));
        return std::string{buffer};
    }

    std::string FormatPanelGradientAxis(const std::uint32_t axis)
    {
        for (const AxisName &candidate : kAxisNames)
        {
            if (candidate.value == axis)
            {
                return candidate.name;
            }
        }
        return "unknown";
    }

    PanelCommandRegistrationResult RegisterPanelCommands(
        runtime::command::CommandRegistry &registry, PanelCommandResolvers resolvers)
    {
        PanelCommandRegistrationResult result;
        if (!resolvers.report || !resolvers.set_text || !resolvers.set_appearance)
        {
            result.diagnostic = "panel command provider requires every resolver";
            return result;
        }

        runtime::command::CommandDesc report_descriptor{
            "panel.report",
            kOwner,
            "Report the loaded panel's glyph product, dot extent, and appearance",
            runtime::command::CommandCategory::Engine,
            runtime::command::CommandFlags::AgentAllowed |
                runtime::command::CommandFlags::LuaAllowed,
            {},
            [resolver = resolvers.report](const runtime::command::CommandCall &call,
                                          const runtime::command::CommandContext &context)
            {
                PanelRuntimeReport panel_report;
                if (!resolver(panel_report))
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::Failed,
                        "No panel is loaded in the current mode", context.request_id, {}};
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
                     {"text", panel_report.text},
                     {"dot_gap", static_cast<double>(panel_report.dot_gap)},
                     {"dot_color", panel_report.dot_color},
                     {"background_color", panel_report.background_color},
                     {"accent_color", panel_report.accent_color},
                     {"gradient_amount",
                      static_cast<double>(panel_report.gradient_amount)},
                     {"gradient_axis", panel_report.gradient_axis},
                     {"cycles_per_second",
                      static_cast<double>(panel_report.cycles_per_second)}}};
            },
            runtime::command::CommandThread::Game};

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
            [resolver = std::move(resolvers.set_text)](
                const runtime::command::CommandCall &call,
                const runtime::command::CommandContext &context)
            {
                const auto iterator = call.arguments.find("text");
                if (iterator == call.arguments.end())
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::InvalidArguments,
                        "panel.set_text requires a text argument", context.request_id, {}};
                }
                const auto *value = std::get_if<std::string>(&iterator->second);
                if (value == nullptr)
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::InvalidArguments,
                        "panel.set_text requires text as a string", context.request_id, {}};
                }

                std::string diagnostic;
                if (!resolver(*value, diagnostic))
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
            result.registrations.clear();
            result.diagnostic = text_registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(text_registration.registration));

        runtime::command::CommandDesc set_appearance_descriptor{
            "panel.set_appearance",
            kOwner,
            "Change the panel's dot gap and colours; any omitted field is kept",
            runtime::command::CommandCategory::Engine,
            runtime::command::CommandFlags::AgentAllowed |
                runtime::command::CommandFlags::LuaAllowed |
                runtime::command::CommandFlags::MutatesState,
            {{runtime::command::CommandArgumentDesc{
                 // Float, so a caller must write `0.25` or `0.0` rather than
                 // `0`: the transport types a literal by its value, and the
                 // schema's Float check requires a double. That rejection is the
                 // guard, which is why the handler reads a double directly
                 // instead of also tolerating an integer that cannot arrive.
                 "dot_gap", runtime::command::CommandValueType::Float, false, {}, {}},
              runtime::command::CommandArgumentDesc{
                  "dot_color", runtime::command::CommandValueType::String, false, {}, {}},
              runtime::command::CommandArgumentDesc{"background_color",
                                                    runtime::command::CommandValueType::String,
                                                    false, {}, {}},
              runtime::command::CommandArgumentDesc{
                  "accent_color", runtime::command::CommandValueType::String, false, {}, {}},
              runtime::command::CommandArgumentDesc{
                  "gradient", runtime::command::CommandValueType::Float, false, {}, {}},
              runtime::command::CommandArgumentDesc{
                  "gradient_axis", runtime::command::CommandValueType::Enum, false, {},
                  {"horizontal", "vertical", "mirrored"}},
              runtime::command::CommandArgumentDesc{"cycles_per_second",
                                                    runtime::command::CommandValueType::Float,
                                                    false, {}, {}}}},
            [resolver = std::move(resolvers.set_appearance)](
                const runtime::command::CommandCall &call,
                const runtime::command::CommandContext &context)
            {
                PanelAppearanceUpdate update;

                const auto gap = call.arguments.find("dot_gap");
                if (gap != call.arguments.end())
                {
                    const auto *value = std::get_if<double>(&gap->second);
                    if (value == nullptr || *value < 0.0 || *value > kMaximumDotGap)
                    {
                        return InvalidArguments(
                            context,
                            "panel.set_appearance requires dot_gap between 0.0 and " +
                                std::to_string(kMaximumDotGap));
                    }
                    update.has_dot_gap = true;
                    update.dot_gap = static_cast<float>(*value);
                }

                const auto dot_color = call.arguments.find("dot_color");
                if (dot_color != call.arguments.end())
                {
                    const auto *value = std::get_if<std::string>(&dot_color->second);
                    if (value == nullptr || !ParsePanelColor(*value, update.dot_color))
                    {
                        return InvalidArguments(
                            context, "panel.set_appearance requires dot_color as "
                                                "\"#RRGGBB\"");
                    }
                    update.has_dot_color = true;
                }

                const auto background = call.arguments.find("background_color");
                if (background != call.arguments.end())
                {
                    const auto *value = std::get_if<std::string>(&background->second);
                    if (value == nullptr ||
                        !ParsePanelColor(*value, update.background_color))
                    {
                        return InvalidArguments(
                            context,
                                       "panel.set_appearance requires background_color as "
                                       "\"#RRGGBB\"");
                    }
                    update.has_background_color = true;
                }

                const auto accent = call.arguments.find("accent_color");
                if (accent != call.arguments.end())
                {
                    const auto *value = std::get_if<std::string>(&accent->second);
                    if (value == nullptr || !ParsePanelColor(*value, update.accent_color))
                    {
                        return InvalidArguments(
                            context,
                            "panel.set_appearance requires accent_color as \"#RRGGBB\"");
                    }
                    update.has_accent_color = true;
                }

                const auto gradient = call.arguments.find("gradient");
                if (gradient != call.arguments.end())
                {
                    const auto *value = std::get_if<double>(&gradient->second);
                    if (value == nullptr || *value < 0.0 || *value > 1.0)
                    {
                        return InvalidArguments(
                            context,
                            "panel.set_appearance requires gradient between 0.0 and 1.0");
                    }
                    update.has_gradient_amount = true;
                    update.gradient_amount = static_cast<float>(*value);
                }

                const auto axis = call.arguments.find("gradient_axis");
                if (axis != call.arguments.end())
                {
                    const auto *value = std::get_if<std::string>(&axis->second);
                    bool matched = false;
                    if (value != nullptr)
                    {
                        for (const AxisName &candidate : kAxisNames)
                        {
                            if (*value == candidate.name)
                            {
                                update.has_gradient_axis = true;
                                update.gradient_axis = candidate.value;
                                matched = true;
                                break;
                            }
                        }
                    }
                    if (!matched)
                    {
                        return InvalidArguments(context,
                                                "panel.set_appearance requires "
                                                "gradient_axis horizontal, vertical, or "
                                                "mirrored");
                    }
                }

                const auto cycles = call.arguments.find("cycles_per_second");
                if (cycles != call.arguments.end())
                {
                    const auto *value = std::get_if<double>(&cycles->second);
                    if (value == nullptr || *value < 0.0 ||
                        *value > kMaximumGradientCyclesPerSecond)
                    {
                        return InvalidArguments(
                            context,
                            "panel.set_appearance requires cycles_per_second between 0.0 "
                            "and " +
                                std::to_string(kMaximumGradientCyclesPerSecond));
                    }
                    update.has_cycles_per_second = true;
                    update.cycles_per_second = static_cast<float>(*value);
                }

                if (!update.has_dot_gap && !update.has_dot_color &&
                    !update.has_background_color && !update.has_accent_color &&
                    !update.has_gradient_amount && !update.has_gradient_axis &&
                    !update.has_cycles_per_second)
                {
                    return InvalidArguments(
                            context, "panel.set_appearance requires at least one "
                                            "appearance field");
                }

                std::string diagnostic;
                if (!resolver(update, diagnostic))
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::Failed, std::move(diagnostic),
                        context.request_id, {}};
                }
                return runtime::command::CommandResult{
                    runtime::command::CommandStatus::Success, "Panel appearance changed",
                    context.request_id, {}};
            },
            runtime::command::CommandThread::Game};

        runtime::command::CommandRegistrationResult appearance_registration =
            registry.Register(std::move(set_appearance_descriptor));
        if (!appearance_registration.IsSuccess())
        {
            result.registrations.clear();
            result.diagnostic = appearance_registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(appearance_registration.registration));

        result.succeeded = true;
        return result;
    }
}
