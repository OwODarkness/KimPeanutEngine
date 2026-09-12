#ifndef KPENGINE_RUNTIME_WINDOW_WINDOW_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_WINDOW_WINDOW_COMMAND_PROVIDER_H

#include <functional>

#include "command/command_registry.h"

namespace kpengine
{
    class WindowSystem;
}

namespace kpengine::runtime
{
    // Registers the Runtime command that asks the active window to resize. The
    // resolver runs on the game thread at dispatch time and returns the window
    // the command should act on, or nullptr when the current mode has none. It
    // is resolved per call rather than captured because a host may create its
    // window on the render thread after this registration runs.
    command::CommandRegistrationResult RegisterWindowCommands(
        command::CommandRegistry &registry,
        std::function<WindowSystem *()> window_resolver);
}

#endif // KPENGINE_RUNTIME_WINDOW_WINDOW_COMMAND_PROVIDER_H
