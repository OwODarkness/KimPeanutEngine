#ifndef KPENGINE_RUNTIME_RENDER_SETTINGS_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_RENDER_SETTINGS_COMMAND_PROVIDER_H

#include <functional>

#include "command/command_registry.h"

namespace kpengine::render { class RenderSystem; }

namespace kpengine::runtime
{
    command::CommandRegistrationResult RegisterHeightFogSettingsCommand(
        command::CommandRegistry &registry, std::function<render::RenderSystem *()> resolve_render);
}

#endif
