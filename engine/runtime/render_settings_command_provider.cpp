#include "render_settings_command_provider.h"

#include <utility>

#include "render/render_system.h"

namespace kpengine::runtime
{
    command::CommandRegistrationResult RegisterHeightFogSettingsCommand(
        command::CommandRegistry &registry, std::function<render::RenderSystem *()> resolve_render)
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
             [resolve = std::move(resolve_render)](const command::CommandCall &call,
                                                   const command::CommandContext &context) {
                 auto *const system = resolve ? resolve() : nullptr;
                 if (!system)
                     return command::CommandResult{command::CommandStatus::Failed,
                         "RenderSystem is unavailable", context.request_id, {}};
                 const auto number = [&call](const char *name) {
                     return static_cast<float>(std::get<double>(call.arguments.at(name)));
                 };
                 const render::HeightFogSettings settings{
                     std::get<bool>(call.arguments.at("enabled")),
                     std::get<bool>(call.arguments.at("affect_sky")),
                     number("density"), number("reference_height"), number("height_falloff"),
                     number("max_distance"), {number("red"), number("green"), number("blue")}};
                 if (!system->RequestHeightFogSettings(settings))
                     return command::CommandResult{command::CommandStatus::InvalidArguments,
                         "Height fog requires finite values within the supported ranges", context.request_id, {}};
                 return command::CommandResult{command::CommandStatus::Success,
                     "Height fog settings scheduled", context.request_id, {}};
             }, command::CommandThread::Game});
    }
}
