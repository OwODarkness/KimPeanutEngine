#ifndef KPENGINE_RUNTIME_SCREENSHOT_SCREENSHOT_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_SCREENSHOT_SCREENSHOT_COMMAND_PROVIDER_H

#include "command/command_registry.h"

namespace kpengine::runtime
{
    class RuntimeScreenshotService;

    // Registers the Runtime-facing command adapter. The returned token must
    // outlive the service reference captured by the command handler.
    command::CommandRegistrationResult RegisterScreenshotCommands(
        command::CommandRegistry &registry, RuntimeScreenshotService &screenshot_service);
}

#endif
