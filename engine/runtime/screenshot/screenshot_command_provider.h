#ifndef KPENGINE_RUNTIME_SCREENSHOT_SCREENSHOT_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_SCREENSHOT_SCREENSHOT_COMMAND_PROVIDER_H

#include <memory>

#include "command/command_registry.h"

namespace kpengine::runtime
{
    class RuntimeScreenshotService;

    // Registers the Runtime-facing command adapter. The handler retains this
    // shared service ownership for any dispatch already accepted by the registry.
    command::CommandRegistrationResult RegisterScreenshotCommands(
        command::CommandRegistry &registry,
        std::shared_ptr<RuntimeScreenshotService> screenshot_service);
}

#endif
