#include "command/command_agent_endpoint.h"

#include <type_traits>

#include <nlohmann/json.hpp>

namespace kpengine::runtime::command
{
    namespace
    {
        using Json = nlohmann::json;

        const char *StatusName(CommandStatus status)
        {
            switch (status)
            {
            case CommandStatus::Success: return "success";
            case CommandStatus::InvalidArguments: return "invalid_arguments";
            case CommandStatus::NotFound: return "not_found";
            case CommandStatus::Denied: return "denied";
            case CommandStatus::Busy: return "busy";
            case CommandStatus::Pending: return "pending";
            case CommandStatus::Failed: return "failed";
            case CommandStatus::Cancelled: return "cancelled";
            case CommandStatus::Shutdown: return "shutdown";
            case CommandStatus::WrongThread: return "wrong_thread";
            }
            return "failed";
        }

        Json ToJson(const CommandValue &value)
        {
            return std::visit([](const auto &item) -> Json
            {
                using Value = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Value, std::monostate>)
                {
                    return nullptr;
                }
                else
                {
                    return item;
                }
            }, value);
        }

        Json ToJson(const CommandResult &result)
        {
            Json data = Json::object();
            for (const auto &[name, value] : result.data)
            {
                data[name] = ToJson(value);
            }
            return {{"status", StatusName(result.status)}, {"message", result.message},
                    {"request_id", result.request_id}, {"data", std::move(data)}};
        }

        std::optional<CommandValue> ParseValue(const Json &value)
        {
            if (value.is_boolean()) return CommandValue{value.get<bool>()};
            if (value.is_number_unsigned()) return CommandValue{value.get<uint64_t>()};
            if (value.is_number_integer()) return CommandValue{value.get<int64_t>()};
            if (value.is_number_float()) return CommandValue{value.get<double>()};
            if (value.is_string()) return CommandValue{value.get<std::string>()};
            return std::nullopt;
        }
    }

    CommandAgentEndpoint::CommandAgentEndpoint(CommandRegistry &registry,
                                               CommandCapability capabilities)
        : registry_(registry), capabilities_(capabilities) {}

    std::vector<CommandDesc> CommandAgentEndpoint::List() const { return registry_.List(); }

    CommandResult CommandAgentEndpoint::Execute(const CommandCall &call) const
    {
        return registry_.Execute(call, {CommandOrigin::Agent, CommandThread::Immediate, capabilities_});
    }

    std::optional<CommandResult> CommandAgentEndpoint::Poll(uint64_t request_id) const
    {
        return registry_.TakeCompletion(request_id);
    }

    bool CommandAgentEndpoint::Cancel(uint64_t request_id) const
    {
        return registry_.CancelRequest(request_id);
    }

    std::string CommandAgentEndpoint::HandleJsonLine(std::string_view request) const
    {
        try
        {
            const Json input = Json::parse(request);
            const std::string operation = input.value("op", "");
            if (operation == "list")
            {
                Json commands = Json::array();
                for (const CommandDesc &descriptor : List())
                {
                    commands.push_back({{"name", descriptor.name}, {"provider", descriptor.provider},
                                        {"help", descriptor.help}});
                }
                return Json{{"status", "success"}, {"commands", std::move(commands)}}.dump();
            }
            if (operation == "execute")
            {
                CommandCall call{input.value("command", ""), {}};
                if (input.contains("arguments") && input["arguments"].is_object())
                {
                    for (const auto &[name, value] : input["arguments"].items())
                    {
                        const auto parsed = ParseValue(value);
                        if (!parsed.has_value())
                        {
                            return ToJson(CommandResult{CommandStatus::InvalidArguments,
                                                        "argument '" + name + "' has unsupported JSON type", 0, {}}).dump();
                        }
                        call.arguments.emplace(name, *parsed);
                    }
                }
                return ToJson(Execute(call)).dump();
            }
            if (operation == "poll")
            {
                const uint64_t request_id = input.value("request_id", uint64_t{0});
                const auto result = Poll(request_id);
                return result.has_value() ? ToJson(*result).dump()
                                          : Json{{"status", "pending"}, {"request_id", request_id}}.dump();
            }
            if (operation == "cancel")
            {
                const uint64_t request_id = input.value("request_id", uint64_t{0});
                return Json{{"status", Cancel(request_id) ? "cancelled" : "not_found"},
                            {"request_id", request_id}}.dump();
            }
            return ToJson(CommandResult{CommandStatus::InvalidArguments,
                                        "op must be list, execute, poll, or cancel", 0, {}}).dump();
        }
        catch (const Json::exception &exception)
        {
            return ToJson(CommandResult{CommandStatus::InvalidArguments, exception.what(), 0, {}}).dump();
        }
    }
}
