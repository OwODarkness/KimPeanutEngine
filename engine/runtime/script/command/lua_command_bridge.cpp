#include "script/command/lua_command_bridge.h"

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <sol/sol.hpp>

#include "command/command_registry.h"
#include "script/lua/lua_vm.h"

namespace kpengine::runtime::script
{
    namespace
    {
        constexpr const char* kEngineTable = "engine";
        constexpr const char* kCommandTable = "command";

        const char* StatusName(const command::CommandStatus status)
        {
            switch (status)
            {
            case command::CommandStatus::Success: return "success";
            case command::CommandStatus::InvalidArguments: return "invalid_arguments";
            case command::CommandStatus::NotFound: return "not_found";
            case command::CommandStatus::Denied: return "denied";
            case command::CommandStatus::Busy: return "busy";
            case command::CommandStatus::Pending: return "pending";
            case command::CommandStatus::Failed: return "failed";
            case command::CommandStatus::Cancelled: return "cancelled";
            case command::CommandStatus::Shutdown: return "shutdown";
            case command::CommandStatus::WrongThread: return "wrong_thread";
            }
            return "failed";
        }

        sol::table MakeResult(sol::state_view state, const command::CommandResult& result)
        {
            sol::table output = state.create_table();
            output["status"] = StatusName(result.status);
            output["message"] = result.message;
            output["request_id"] = result.request_id;
            sol::table data = state.create_table();
            for (const auto& [name, value] : result.data)
            {
                std::visit([&data, &name](const auto& item)
                {
                    using Value = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<Value, std::monostate>)
                    {
                        data[name] = sol::nil;
                    }
                    else
                    {
                        data[name] = item;
                    }
                }, value);
            }
            output["data"] = std::move(data);
            return output;
        }

        bool IsVisibleToLua(const command::CommandDesc& descriptor,
                            const command::CommandCapability capabilities)
        {
            using namespace command;
            if (!HasCommandFlag(descriptor.flags, CommandFlags::LuaAllowed)) return false;
            if (HasCommandFlag(descriptor.flags, CommandFlags::DevelopmentOnly) &&
                !HasCommandCapability(capabilities, CommandCapability::Development)) return false;
            if (HasCommandFlag(descriptor.flags, CommandFlags::EditorOnly) &&
                !HasCommandCapability(capabilities, CommandCapability::Editor)) return false;
            if (HasCommandFlag(descriptor.flags, CommandFlags::MutatesState) &&
                !HasCommandCapability(capabilities, CommandCapability::Mutating)) return false;
            return !HasCommandFlag(descriptor.flags, CommandFlags::Destructive) ||
                   HasCommandCapability(capabilities, CommandCapability::Destructive);
        }

        sol::table MakeDescriptor(sol::state_view state, const command::CommandDesc& descriptor)
        {
            sol::table output = state.create_table();
            output["name"] = descriptor.name;
            output["provider"] = descriptor.provider;
            output["help"] = descriptor.help;
            sol::table arguments = state.create_table();
            for (const command::CommandArgumentDesc& argument : descriptor.schema.arguments)
            {
                sol::table item = state.create_table();
                item["name"] = argument.name;
                item["required"] = argument.required;
                item["type"] = static_cast<uint32_t>(argument.type);
                sol::table enum_values = state.create_table();
                for (std::size_t index = 0; index < argument.enum_values.size(); ++index)
                {
                    enum_values[index + 1] = argument.enum_values[index];
                }
                item["enum_values"] = std::move(enum_values);
                arguments[arguments.size() + 1] = std::move(item);
            }
            output["arguments"] = std::move(arguments);
            return output;
        }

        std::optional<command::CommandValue> ParseLuaValue(const sol::object& value,
                                                            const command::CommandArgumentDesc* descriptor)
        {
            if (descriptor == nullptr)
            {
                if (value.is<bool>()) return command::CommandValue{value.as<bool>()};
                if (value.is<int64_t>()) return command::CommandValue{value.as<int64_t>()};
                if (value.is<double>()) return command::CommandValue{value.as<double>()};
                if (value.is<std::string>()) return command::CommandValue{value.as<std::string>()};
                return std::nullopt;
            }
            const command::CommandValueType type = descriptor->type;
            switch (type)
            {
            case command::CommandValueType::Boolean:
                return value.is<bool>() ? std::optional<command::CommandValue>{value.as<bool>()} : std::nullopt;
            case command::CommandValueType::SignedInteger:
                return value.is<int64_t>() ? std::optional<command::CommandValue>{value.as<int64_t>()} : std::nullopt;
            case command::CommandValueType::UnsignedInteger:
                if (!value.is<int64_t>()) return std::nullopt;
                {
                    const int64_t signed_value = value.as<int64_t>();
                    return signed_value < 0 ? std::nullopt
                                            : std::optional<command::CommandValue>{static_cast<uint64_t>(signed_value)};
                }
            case command::CommandValueType::Float:
                return value.is<double>() ? std::optional<command::CommandValue>{value.as<double>()} : std::nullopt;
            case command::CommandValueType::String:
            case command::CommandValueType::Enum:
                return value.is<std::string>()
                           ? std::optional<command::CommandValue>{value.as<std::string>()}
                           : std::nullopt;
            }
            return std::nullopt;
        }
    }

    LuaCommandBridge::LuaCommandBridge(command::CommandRegistry& registry,
                                       ::kpengine::script::lua::LuaVM& lua_vm,
                                       const command::CommandCapability capabilities)
        : registry_(registry), lua_vm_(lua_vm), capabilities_(capabilities) {}

    LuaCommandBridge::~LuaCommandBridge() { Shutdown(); }

    bool LuaCommandBridge::Initialize()
    {
        if (initialized_ || !lua_vm_.IsInitialized()) return false;
        lua_thread_id_ = std::this_thread::get_id();
        const bool registered =
            lua_vm_.RegisterNestedTableFunction(kEngineTable, kCommandTable, "list",
                [this](sol::this_state callback_state)
                {
                    sol::state_view callback{callback_state};
                    if (std::this_thread::get_id() != lua_thread_id_)
                    {
                        return MakeResult(callback, {command::CommandStatus::WrongThread,
                                                     "Lua command calls must run on the Game/Lua thread", 0, {}});
                    }
                    sol::table results = callback.create_table();
                    for (const command::CommandDesc& descriptor : registry_.List())
                    {
                        if (IsVisibleToLua(descriptor, capabilities_))
                        {
                            results[results.size() + 1] = MakeDescriptor(callback, descriptor);
                        }
                    }
                    return results;
                }) &&
            lua_vm_.RegisterNestedTableFunction(kEngineTable, kCommandTable, "help",
                [this](sol::this_state callback_state, const std::string& name)
                {
                    sol::state_view callback{callback_state};
                    if (std::this_thread::get_id() != lua_thread_id_)
                    {
                        return MakeResult(callback, {command::CommandStatus::WrongThread,
                                                     "Lua command calls must run on the Game/Lua thread", 0, {}});
                    }
                    const auto descriptor = registry_.Find(name);
                    if (!descriptor.has_value() || !IsVisibleToLua(*descriptor, capabilities_))
                    {
                        return MakeResult(callback, {command::CommandStatus::NotFound,
                                                     "Command is not available to Lua", 0, {}});
                    }
                    sol::table result = MakeResult(callback, {command::CommandStatus::Success, "", 0, {}});
                    result["command"] = MakeDescriptor(callback, *descriptor);
                    return result;
                }) &&
            lua_vm_.RegisterNestedTableFunction(kEngineTable, kCommandTable, "execute",
                [this](sol::this_state callback_state, const std::string& name,
                       sol::optional<sol::table> arguments)
                {
                    sol::state_view callback{callback_state};
                    if (std::this_thread::get_id() != lua_thread_id_)
                    {
                        return MakeResult(callback, {command::CommandStatus::WrongThread,
                                                     "Lua command calls must run on the Game/Lua thread", 0, {}});
                    }
                    command::CommandCall call{name, {}};
                    const auto descriptor = registry_.Find(name);
                    if (arguments.has_value())
                    {
                        for (const auto& pair : *arguments)
                        {
                            if (pair.first.get_type() != sol::type::string)
                            {
                                return MakeResult(callback, {command::CommandStatus::InvalidArguments,
                                                             "Lua command argument names must be strings", 0, {}});
                            }
                            const std::string argument_name = pair.first.as<std::string>();
                            const command::CommandArgumentDesc* argument_descriptor = nullptr;
                            if (descriptor.has_value())
                            {
                                for (const command::CommandArgumentDesc& candidate : descriptor->schema.arguments)
                                {
                                    if (candidate.name == argument_name)
                                    {
                                        argument_descriptor = &candidate;
                                        break;
                                    }
                                }
                            }
                            const auto value = ParseLuaValue(pair.second, argument_descriptor);
                            if (!value.has_value())
                            {
                                return MakeResult(callback, {command::CommandStatus::InvalidArguments,
                                                             "Lua value does not match command argument type", 0, {}});
                            }
                            call.arguments.emplace(argument_name, *value);
                        }
                    }
                    return MakeResult(callback, registry_.Execute(
                        call, {command::CommandOrigin::Lua, command::CommandThread::Immediate, capabilities_}));
                }) &&
            lua_vm_.RegisterNestedTableFunction(kEngineTable, kCommandTable, "poll",
                [this](sol::this_state callback_state, const uint64_t request_id)
                {
                    sol::state_view callback{callback_state};
                    if (std::this_thread::get_id() != lua_thread_id_)
                    {
                        return MakeResult(callback, {command::CommandStatus::WrongThread,
                                                     "Lua command calls must run on the Game/Lua thread", 0, {}});
                    }
                    const auto result = registry_.TakeCompletion(request_id);
                    return result.has_value()
                               ? MakeResult(callback, *result)
                               : MakeResult(callback, {command::CommandStatus::Pending, "", request_id, {}});
                }) &&
            lua_vm_.RegisterNestedTableFunction(kEngineTable, kCommandTable, "cancel",
                [this](sol::this_state callback_state, const uint64_t request_id)
                {
                    sol::state_view callback{callback_state};
                    if (std::this_thread::get_id() != lua_thread_id_)
                    {
                        return MakeResult(callback, {command::CommandStatus::WrongThread,
                                                     "Lua command calls must run on the Game/Lua thread", 0, {}});
                    }
                    return MakeResult(callback, {registry_.CancelRequest(request_id)
                                                     ? command::CommandStatus::Cancelled
                                                     : command::CommandStatus::NotFound,
                                                 "", request_id, {}});
                });
        if (!registered) return false;
        initialized_ = true;
        return true;
    }

    void LuaCommandBridge::Shutdown()
    {
        if (!initialized_) return;
        lua_vm_.RemoveNestedTableFunction(kEngineTable, kCommandTable, "cancel");
        lua_vm_.RemoveNestedTableFunction(kEngineTable, kCommandTable, "poll");
        lua_vm_.RemoveNestedTableFunction(kEngineTable, kCommandTable, "execute");
        lua_vm_.RemoveNestedTableFunction(kEngineTable, kCommandTable, "help");
        lua_vm_.RemoveNestedTableFunction(kEngineTable, kCommandTable, "list");
        initialized_ = false;
    }
}
