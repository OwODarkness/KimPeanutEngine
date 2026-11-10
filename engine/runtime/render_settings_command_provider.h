#ifndef KPENGINE_RUNTIME_RENDER_SETTINGS_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_RENDER_SETTINGS_COMMAND_PROVIDER_H

#include <functional>
#include <string_view>

#include "command/command_registry.h"
#include "settings/settings_types.h"

namespace kpengine::runtime
{
    command::CommandRegistrationResult RegisterHeightFogSettingsCommand(
        command::CommandRegistry &registry,
        std::function<command::CommandResult(
            std::string_view, settings::SettingValues,
            const command::CommandContext &)> submit_settings_edit);
}

#endif
