#ifndef KPENGINE_RUNTIME_COMMAND_COMMAND_TYPES_H
#define KPENGINE_RUNTIME_COMMAND_COMMAND_TYPES_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace kpengine::runtime::command
{
    enum class CommandCategory : uint8_t
    {
        Engine,
        Render,
        Gameplay,
        Script,
        Debug,
        Test,
    };

    enum class CommandOrigin : uint8_t
    {
        UserConsole,
        Agent,
        Lua,
        Test,
        Cli,
    };

    enum class CommandThread : uint8_t
    {
        Immediate,
        Game,
        Render,
        Async,
    };

    enum class CommandStatus : uint8_t
    {
        Success,
        InvalidArguments,
        NotFound,
        Denied,
        Busy,
        Pending,
        Failed,
        Cancelled,
        Shutdown,
        WrongThread,
    };

    enum class CommandFlags : uint32_t
    {
        None = 0,
        DevelopmentOnly = 1u << 0,
        EditorOnly = 1u << 1,
        LuaAllowed = 1u << 2,
        AgentAllowed = 1u << 3,
        MutatesState = 1u << 4,
        Destructive = 1u << 5,
    };

    constexpr CommandFlags operator|(CommandFlags left, CommandFlags right) noexcept
    {
        return static_cast<CommandFlags>(static_cast<uint32_t>(left) |
                                         static_cast<uint32_t>(right));
    }

    constexpr CommandFlags operator&(CommandFlags left, CommandFlags right) noexcept
    {
        return static_cast<CommandFlags>(static_cast<uint32_t>(left) &
                                         static_cast<uint32_t>(right));
    }

    constexpr bool HasCommandFlag(CommandFlags flags, CommandFlags flag) noexcept
    {
        return (flags & flag) != CommandFlags::None;
    }

    enum class CommandCapability : uint32_t
    {
        None = 0,
        Development = 1u << 0,
        Editor = 1u << 1,
        Mutating = 1u << 2,
        Destructive = 1u << 3,
    };

    constexpr CommandCapability operator|(CommandCapability left,
                                          CommandCapability right) noexcept
    {
        return static_cast<CommandCapability>(static_cast<uint32_t>(left) |
                                               static_cast<uint32_t>(right));
    }

    constexpr CommandCapability operator&(CommandCapability left,
                                          CommandCapability right) noexcept
    {
        return static_cast<CommandCapability>(static_cast<uint32_t>(left) &
                                               static_cast<uint32_t>(right));
    }

    constexpr bool HasCommandCapability(CommandCapability capabilities,
                                        CommandCapability capability) noexcept
    {
        return (capabilities & capability) != CommandCapability::None;
    }

    enum class CommandValueType : uint8_t
    {
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Float,
        String,
        Enum,
    };

    using CommandValue = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string>;
    using CommandData = std::map<std::string, CommandValue>;
    using CommandArguments = std::map<std::string, CommandValue>;

    struct CommandArgumentDesc
    {
        std::string name;
        CommandValueType type = CommandValueType::String;
        bool required = false;
        CommandValue default_value;
        std::vector<std::string> enum_values;
    };

    struct CommandSchema
    {
        std::vector<CommandArgumentDesc> arguments;
    };

    struct CommandCall
    {
        std::string name;
        CommandArguments arguments;
    };

    struct CommandResult
    {
        CommandStatus status = CommandStatus::Failed;
        std::string message;
        uint64_t request_id = 0;
        CommandData data;

        bool IsSuccess() const noexcept { return status == CommandStatus::Success; }
    };

    // Installed by the registry only while it is dispatching a deferred
    // request. A handler uses it to finish work that outlives its invocation.
    using CommandCompletionSink = std::function<void(CommandResult)>;

    struct CommandContext
    {
        CommandContext() = default;
        CommandContext(CommandOrigin in_origin, CommandThread in_thread,
                       CommandCapability in_capabilities = CommandCapability::None)
            : origin(in_origin), thread(in_thread), capabilities(in_capabilities)
        {
        }

        CommandOrigin origin = CommandOrigin::Test;
        CommandThread thread = CommandThread::Immediate;
        CommandCapability capabilities = CommandCapability::None;
        uint64_t request_id = 0;
        CommandCompletionSink complete;
    };

    using CommandHandler = std::function<CommandResult(const CommandCall &, const CommandContext &)>;

    struct CommandDesc
    {
        std::string name;
        std::string provider;
        std::string help;
        CommandCategory category = CommandCategory::Engine;
        CommandFlags flags = CommandFlags::None;
        CommandSchema schema;
        CommandHandler handler;
        CommandThread execution_thread = CommandThread::Immediate;
    };
}

#endif
