#ifndef KPENGINE_RUNTIME_COMMAND_COMMAND_AGENT_ENDPOINT_H
#define KPENGINE_RUNTIME_COMMAND_COMMAND_AGENT_ENDPOINT_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_registry.h"

namespace kpengine::runtime::command
{
    // Structured, headless-facing adapter. It contains no Editor, window, or
    // transport state; hosts may send each JSON response over stdin/stdout, IPC,
    // or an automation connection.
    class CommandAgentEndpoint final
    {
    public:
        explicit CommandAgentEndpoint(CommandRegistry &registry,
                                      CommandCapability capabilities = CommandCapability::None);

        std::vector<CommandDesc> List() const;
        CommandResult Execute(const CommandCall &call) const;
        std::optional<CommandResult> Poll(uint64_t request_id) const;
        bool Cancel(uint64_t request_id) const;

        // Accepts one JSON object and returns one JSON object. Supported ops are
        // "list", "execute", "poll", and "cancel".
        std::string HandleJsonLine(std::string_view request) const;

    private:
        CommandRegistry &registry_;
        CommandCapability capabilities_;
    };
}

#endif
