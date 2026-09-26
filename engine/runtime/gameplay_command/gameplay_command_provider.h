#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMMAND_PROVIDER_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMMAND_PROVIDER_H

#include <array>
#include <functional>
#include <string>

#include "command/command_registry.h"

namespace kpengine::gameplay
{
    class GameplayWorld;
}

namespace kpengine::runtime
{
    struct GameplayCommandRegistrationResult
    {
        std::array<command::CommandRegistration, 3> registrations;
        command::CommandRegistrationStatus status =
            command::CommandRegistrationStatus::InvalidDescriptor;
        std::string diagnostic;

        bool IsSuccess() const noexcept
        {
            return status == command::CommandRegistrationStatus::Registered &&
                   registrations[0].IsValid() && registrations[1].IsValid() &&
                   registrations[2].IsValid();
        }
    };

    GameplayCommandRegistrationResult RegisterGameplayCommands(
        command::CommandRegistry &registry,
        std::function<gameplay::GameplayWorld *()> world_resolver);
}

#endif
