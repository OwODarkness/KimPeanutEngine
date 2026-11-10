#include "render_settings_command_provider.h"

#include <utility>

#include "render/settings/render_settings.h"

namespace kpengine::runtime
{
    command::CommandRegistrationResult RegisterHeightFogSettingsCommand(
        command::CommandRegistry &registry,
        std::function<command::CommandResult(
            std::string_view, settings::SettingValues,
            const command::CommandContext &)> submit_settings_edit)
    {
        const auto argument = [](const char *name, command::CommandValueType type) {
            return command::CommandArgumentDesc{name, type, true, {}, {}};
        };
        using Type = command::CommandValueType;
        return registry.Register(
            {"render.height_fog_settings", "RenderSettings", "Set raster exponential height fog",
             command::CommandCategory::Render,
             command::CommandFlags::AgentAllowed | command::CommandFlags::MutatesState,
             {{argument("enabled", Type::Boolean), argument("affect_sky", Type::Boolean),
              argument("density", Type::Float), argument("reference_height", Type::Float),
              argument("height_falloff", Type::Float), argument("max_distance", Type::Float),
              argument("red", Type::Float), argument("green", Type::Float), argument("blue", Type::Float)}},
             [submit = std::move(submit_settings_edit)](const command::CommandCall &call,
                                                        const command::CommandContext &context) {
                 if (!submit)
                     return command::CommandResult{command::CommandStatus::Failed,
                         "Render settings edit route is unavailable", context.request_id, {}};
                 const auto number = [&call](const char *name) {
                     return static_cast<float>(std::get<double>(call.arguments.at(name)));
                 };
                 const render::HeightFogSettings settings{
                     std::get<bool>(call.arguments.at("enabled")),
                     std::get<bool>(call.arguments.at("affect_sky")),
                     number("density"), number("reference_height"), number("height_falloff"),
                     number("max_distance"), {number("red"), number("green"), number("blue")}};
                 return submit("render.height_fog",
                               render::MakeHeightFogSettingValues(settings), context);
             }, command::CommandThread::Game});
    }
}
