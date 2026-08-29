#ifndef KPENGINE_RUNTIME_COMMAND_COMMAND_PARSER_H
#define KPENGINE_RUNTIME_COMMAND_COMMAND_PARSER_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_types.h"

namespace kpengine::runtime::command
{
    struct CommandParseResult
    {
        std::optional<CommandCall> call;
        std::string diagnostic;

        bool IsSuccess() const noexcept { return call.has_value(); }
    };

    class CommandParser final
    {
    public:
        // Parses command text and validates the result against descriptor.schema.
        // The returned call contains typed values and applied schema defaults.
        static CommandParseResult Parse(std::string_view text,
                                         const CommandDesc &descriptor);

        // Validates a structured call and applies schema defaults. Handlers must
        // receive this normalized result rather than the caller's raw call.
        static CommandParseResult Validate(const CommandCall &call,
                                           const CommandDesc &descriptor);

        // Validates a descriptor schema before registration.
        static bool ValidateSchema(const CommandSchema &schema,
                                    std::string &diagnostic);

        // Produces deterministic human- and agent-readable schema help.
        static std::string FormatHelp(const CommandDesc &descriptor);

        // Reads the first lexical token so a text frontend can resolve the
        // descriptor before performing schema validation.
        static std::optional<std::string> ExtractCommandName(std::string_view text,
                                                              std::string &diagnostic);

        // Returns deterministic replacement candidates for a partial command
        // line. Candidates are command names, argument names ending in '=', or
        // enum values (with the argument prefix for named values).
        static std::vector<std::string> Complete(
            std::string_view text,
            const std::vector<CommandDesc> &descriptors);
    };
}

#endif
