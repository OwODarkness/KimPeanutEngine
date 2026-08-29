#include "command/command_registry.h"
#include "command/command_parser.h"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace kpengine::runtime::command
{
    struct CommandRegistry::State
    {
        enum class RequestState : uint8_t
        {
            Queued,
            Running,
            Complete,
        };

        struct Entry
        {
            CommandDesc descriptor;
            uint64_t registration_id = 0;
        };

        struct Request
        {
            CommandCall call;
            CommandContext context;
            CommandCompletionHandler completion;
            RequestState state = RequestState::Queued;
            std::optional<CommandResult> result;
        };

        mutable std::mutex mutex;
        std::unordered_map<std::string, Entry> entries;
        std::deque<uint64_t> game_queue;
        std::map<uint64_t, Request> requests;
        uint64_t next_registration_id = 1;
        uint64_t next_request_id = 1;
        bool shutdown = false;

        void Unregister(const std::string &name, uint64_t registration_id)
        {
            std::scoped_lock lock(mutex);
            const auto iterator = entries.find(name);
            if (iterator != entries.end() && iterator->second.registration_id == registration_id)
            {
                entries.erase(iterator);
            }
        }
    };

    namespace
    {
        constexpr const char *kRuntimeCommandProvider = "RuntimeCommand";

        std::string GetProviderName(const CommandDesc &descriptor)
        {
            return descriptor.provider.empty() ? "<unnamed>" : descriptor.provider;
        }

        std::optional<std::string> GetAuthorizationFailure(const CommandDesc &descriptor,
                                                            const CommandContext &context)
        {
            if (HasCommandFlag(descriptor.flags, CommandFlags::DevelopmentOnly) &&
                !HasCommandCapability(context.capabilities, CommandCapability::Development))
            {
                return "Command requires development capability";
            }
            if (HasCommandFlag(descriptor.flags, CommandFlags::EditorOnly) &&
                !HasCommandCapability(context.capabilities, CommandCapability::Editor))
            {
                return "Command requires editor capability";
            }
            if (context.origin == CommandOrigin::Lua &&
                !HasCommandFlag(descriptor.flags, CommandFlags::LuaAllowed))
            {
                return "Command is not allowed from Lua";
            }
            if (context.origin == CommandOrigin::Agent &&
                !HasCommandFlag(descriptor.flags, CommandFlags::AgentAllowed))
            {
                return "Command is not allowed from an agent";
            }
            if (HasCommandFlag(descriptor.flags, CommandFlags::MutatesState) &&
                !HasCommandCapability(context.capabilities, CommandCapability::Mutating))
            {
                return "Command requires mutating capability";
            }
            if (HasCommandFlag(descriptor.flags, CommandFlags::Destructive) &&
                !HasCommandCapability(context.capabilities, CommandCapability::Destructive))
            {
                return "Command requires destructive capability";
            }
            return std::nullopt;
        }

    }

    CommandResult CommandRegistry::MakeCommandListResult(const std::shared_ptr<State> &state)
    {
        std::vector<std::string> names;
        {
            std::scoped_lock lock(state->mutex);
            names.reserve(state->entries.size());
            for (const auto &item : state->entries)
            {
                names.push_back(item.first);
            }
        }
        std::sort(names.begin(), names.end());

        std::ostringstream output;
        for (size_t index = 0; index < names.size(); ++index)
        {
            if (index != 0)
            {
                output << '\n';
            }
            output << names[index];
        }
        return {CommandStatus::Success, output.str(), 0,
                {{"count", static_cast<uint64_t>(names.size())}}};
    }

    CommandResult CommandRegistry::MakeHelpResult(const std::shared_ptr<State> &state,
                                                  const CommandCall &call)
    {
        const auto argument = call.arguments.find("name");
        if (argument == call.arguments.end())
        {
            return MakeCommandListResult(state);
        }
        const std::string *const requested_name = std::get_if<std::string>(&argument->second);
        if (requested_name == nullptr || requested_name->empty())
        {
            return {CommandStatus::InvalidArguments,
                    "help argument 'name' must be a non-empty string", 0, {}};
        }

        CommandDesc descriptor;
        {
            std::scoped_lock lock(state->mutex);
            const auto entry = state->entries.find(*requested_name);
            if (entry == state->entries.end())
            {
                return {CommandStatus::NotFound, "Command is not registered", 0, {}};
            }
            descriptor = entry->second.descriptor;
        }

        return {CommandStatus::Success, CommandParser::FormatHelp(descriptor), 0,
                {{"name", descriptor.name}, {"provider", descriptor.provider}}};
    }

    void CommandRegistry::InstallBuiltins(const std::shared_ptr<State> &state)
    {
        const std::weak_ptr<State> weak_state = state;
        std::scoped_lock lock(state->mutex);
        state->entries.emplace(
            "commands.list",
            State::Entry{
                {"commands.list", kRuntimeCommandProvider,
                 "List registered command names", CommandCategory::Engine,
                 CommandFlags::None, {},
                 [weak_state](const CommandCall &, const CommandContext &)
                 {
                     const std::shared_ptr<State> locked_state = weak_state.lock();
                     return locked_state != nullptr
                                ? CommandRegistry::MakeCommandListResult(locked_state)
                                : CommandResult{CommandStatus::Shutdown,
                                                 "Command registry has shut down", 0, {}};
                 }},
                0});
        state->entries.emplace(
            "help",
            State::Entry{
                {"help", kRuntimeCommandProvider,
                 "Show command help; omit 'name' to list commands", CommandCategory::Engine,
                 CommandFlags::None,
                 {{CommandArgumentDesc{"name", CommandValueType::String, false, {}, {}}}},
                 [weak_state](const CommandCall &call, const CommandContext &)
                 {
                     const std::shared_ptr<State> locked_state = weak_state.lock();
                     return locked_state != nullptr
                                ? CommandRegistry::MakeHelpResult(locked_state, call)
                                : CommandResult{CommandStatus::Shutdown,
                                                 "Command registry has shut down", 0, {}};
                 }},
                0});
    }

    CommandRegistration::~CommandRegistration()
    {
        if (release_)
        {
            release_();
            release_ = {};
        }
    }

    CommandRegistration::CommandRegistration(CommandRegistration &&other) noexcept
        : release_(std::move(other.release_))
    {
        other.release_ = {};
    }

    CommandRegistration &CommandRegistration::operator=(CommandRegistration &&other) noexcept
    {
        if (this != &other)
        {
            if (release_)
            {
                release_();
                release_ = {};
            }
            release_ = std::move(other.release_);
            other.release_ = {};
        }
        return *this;
    }

    CommandRegistry::CommandRegistry() : state_(std::make_shared<State>())
    {
        InstallBuiltins(state_);
    }

    CommandRegistry::~CommandRegistry() = default;

    CommandRegistrationResult CommandRegistry::Register(CommandDesc descriptor)
    {
        if (descriptor.name.empty() || !descriptor.handler)
        {
            return {{}, CommandRegistrationStatus::InvalidDescriptor,
                    "Command name and handler are required"};
        }

        std::string schema_diagnostic;
        if (!CommandParser::ValidateSchema(descriptor.schema, schema_diagnostic))
        {
            return {{}, CommandRegistrationStatus::InvalidSchema, schema_diagnostic};
        }

        const std::shared_ptr<State> state = state_;
        const std::string name = descriptor.name;
        const std::string provider = GetProviderName(descriptor);
        uint64_t registration_id = 0;
        {
            std::scoped_lock lock(state->mutex);
            if (state->shutdown)
            {
                return {{}, CommandRegistrationStatus::Shutdown,
                        "Command registry has shut down"};
            }
            if (state->entries.find(descriptor.name) != state->entries.end())
            {
                const CommandDesc &existing = state->entries.at(descriptor.name).descriptor;
                return {{}, CommandRegistrationStatus::DuplicateName,
                        "Command '" + descriptor.name + "' is already registered by provider '" +
                            GetProviderName(existing) + "'"};
            }

            descriptor.provider = provider;
            registration_id = state->next_registration_id++;
            state->entries.emplace(name,
                                    State::Entry{std::move(descriptor), registration_id});
        }

        const std::weak_ptr<State> weak_state = state;
        return {CommandRegistration([weak_state, name, registration_id]
        {
            if (const std::shared_ptr<State> locked_state = weak_state.lock())
            {
                locked_state->Unregister(name, registration_id);
            }
        }), CommandRegistrationStatus::Registered, {}};
    }

    std::optional<CommandDesc> CommandRegistry::Find(const std::string &name) const
    {
        const std::shared_ptr<State> state = state_;
        std::scoped_lock lock(state->mutex);
        const auto iterator = state->entries.find(name);
        return iterator == state->entries.end()
                   ? std::nullopt
                   : std::optional<CommandDesc>{iterator->second.descriptor};
    }

    std::vector<CommandDesc> CommandRegistry::List() const
    {
        const std::shared_ptr<State> state = state_;
        std::vector<CommandDesc> descriptors;
        {
            std::scoped_lock lock(state->mutex);
            descriptors.reserve(state->entries.size());
            for (const auto &item : state->entries)
            {
                descriptors.push_back(item.second.descriptor);
            }
        }
        std::sort(descriptors.begin(), descriptors.end(),
                  [](const CommandDesc &left, const CommandDesc &right)
                  { return left.name < right.name; });
        return descriptors;
    }

    CommandResult CommandRegistry::ExecuteResolved(const std::shared_ptr<State> &state,
                                                   CommandDesc descriptor,
                                                   const CommandCall &call,
                                                   const CommandContext &context,
                                                   CommandCompletionHandler completion)
    {
        const CommandParseResult validated = CommandParser::Validate(call, descriptor);
        if (!validated.IsSuccess())
        {
            return {CommandStatus::InvalidArguments, validated.diagnostic, 0, {}};
        }

        if (const auto authorization_failure = GetAuthorizationFailure(descriptor, context);
            authorization_failure.has_value())
        {
            return {CommandStatus::Denied, *authorization_failure, 0, {}};
        }

        if (descriptor.execution_thread == CommandThread::Game &&
            context.thread != CommandThread::Game)
        {
            return EnqueueGameRequest(state, *validated.call, context, std::move(completion));
        }
        if (descriptor.execution_thread != CommandThread::Immediate &&
            descriptor.execution_thread != context.thread)
        {
            return {CommandStatus::WrongThread,
                    "Command requires a different execution lane", 0, {}};
        }

        // Invoke user code after releasing the registry lock. Handlers may
        // inspect or mutate registrations without deadlocking the registry.
        return descriptor.handler(*validated.call, context);
    }

    CommandResult CommandRegistry::EnqueueGameRequest(const std::shared_ptr<State> &state,
                                                      CommandCall call,
                                                      CommandContext context,
                                                      CommandCompletionHandler completion)
    {
        std::scoped_lock lock(state->mutex);
        if (state->shutdown)
        {
            return {CommandStatus::Shutdown, "Command registry has shut down", 0, {}};
        }

        const uint64_t request_id = state->next_request_id++;
        state->requests.emplace(request_id,
                                 State::Request{std::move(call), context, std::move(completion),
                                                State::RequestState::Queued, {}});
        state->game_queue.push_back(request_id);
        return {CommandStatus::Pending, "Command queued for the game thread", request_id, {}};
    }

    void CommandRegistry::CompleteRequest(const std::shared_ptr<State> &state,
                                          const uint64_t request_id,
                                          CommandResult result)
    {
        CommandCompletionHandler completion;
        {
            std::scoped_lock lock(state->mutex);
            const auto iterator = state->requests.find(request_id);
            if (iterator == state->requests.end() ||
                iterator->second.state == State::RequestState::Complete)
            {
                return;
            }
            result.request_id = request_id;
            iterator->second.result = result;
            iterator->second.state = State::RequestState::Complete;
            completion = std::move(iterator->second.completion);
        }

        if (completion)
        {
            completion(result);
        }
    }

    CommandResult CommandRegistry::Execute(const CommandCall &call,
                                           const CommandContext &context,
                                           CommandCompletionHandler completion) const
    {
        CommandDesc descriptor;
        const std::shared_ptr<State> state = state_;
        {
            std::scoped_lock lock(state->mutex);
            if (state->shutdown)
            {
                return {CommandStatus::Shutdown, "Command registry has shut down", 0, {}};
            }
            const auto iterator = state->entries.find(call.name);
            if (iterator == state->entries.end())
            {
                return {CommandStatus::NotFound, "Command is not registered", 0, {}};
            }
            descriptor = iterator->second.descriptor;
        }

        return ExecuteResolved(state, std::move(descriptor), call, context, std::move(completion));
    }

    CommandResult CommandRegistry::ExecuteText(std::string_view text,
                                                const CommandContext &context,
                                                CommandCompletionHandler completion) const
    {
        std::string diagnostic;
        const std::optional<std::string> command_name =
            CommandParser::ExtractCommandName(text, diagnostic);
        if (!command_name.has_value())
        {
            return {CommandStatus::InvalidArguments, diagnostic, 0, {}};
        }

        const std::optional<CommandDesc> descriptor = Find(*command_name);
        if (!descriptor.has_value())
        {
            return {CommandStatus::NotFound, "Command is not registered", 0, {}};
        }
        const CommandParseResult parsed = CommandParser::Parse(text, *descriptor);
        if (!parsed.IsSuccess())
        {
            return {CommandStatus::InvalidArguments, parsed.diagnostic, 0, {}};
        }
        return Execute(*parsed.call, context, std::move(completion));
    }

    std::size_t CommandRegistry::PumpGameThread(const std::size_t max_commands)
    {
        const std::shared_ptr<State> state = state_;
        std::size_t executed = 0;
        while (executed < max_commands)
        {
            uint64_t request_id = 0;
            CommandCall call;
            CommandContext context;
            {
                std::scoped_lock lock(state->mutex);
                if (state->game_queue.empty())
                {
                    break;
                }
                request_id = state->game_queue.front();
                state->game_queue.pop_front();
                const auto iterator = state->requests.find(request_id);
                if (iterator == state->requests.end() ||
                    iterator->second.state != State::RequestState::Queued)
                {
                    continue;
                }
                iterator->second.state = State::RequestState::Running;
                call = iterator->second.call;
                context = iterator->second.context;
                context.thread = CommandThread::Game;
                context.request_id = request_id;
                const std::weak_ptr<State> weak_state = state;
                context.complete = [weak_state, request_id](CommandResult result)
                {
                    if (const std::shared_ptr<State> locked_state = weak_state.lock())
                    {
                        CommandRegistry::CompleteRequest(locked_state, request_id,
                                                         std::move(result));
                    }
                };
            }

            const CommandResult result = Execute(call, context);
            // Pending means the handler transferred completion ownership to an
            // asynchronous subsystem through CommandContext::complete.
            if (result.status != CommandStatus::Pending)
            {
                CompleteRequest(state, request_id, result);
            }
            ++executed;
        }
        return executed;
    }

    std::optional<CommandResult> CommandRegistry::TakeCompletion(const uint64_t request_id)
    {
        const std::shared_ptr<State> state = state_;
        std::scoped_lock lock(state->mutex);
        const auto iterator = state->requests.find(request_id);
        if (iterator == state->requests.end() || !iterator->second.result.has_value())
        {
            return std::nullopt;
        }
        CommandResult result = std::move(*iterator->second.result);
        state->requests.erase(iterator);
        return result;
    }

    std::size_t CommandRegistry::PendingRequestCount() const
    {
        const std::shared_ptr<State> state = state_;
        std::scoped_lock lock(state->mutex);
        std::size_t count = 0;
        for (const auto &request : state->requests)
        {
            if (request.second.state != State::RequestState::Complete)
            {
                ++count;
            }
        }
        return count;
    }

    void CommandRegistry::Shutdown()
    {
        const std::shared_ptr<State> state = state_;
        std::vector<std::pair<CommandCompletionHandler, CommandResult>> completions;
        {
            std::scoped_lock lock(state->mutex);
            state->shutdown = true;
            state->entries.clear();
            state->game_queue.clear();
            for (auto &request : state->requests)
            {
                if (request.second.state == State::RequestState::Queued)
                {
                    CommandResult result{CommandStatus::Shutdown,
                                         "Command registry has shut down", request.first, {}};
                    request.second.result = result;
                    request.second.state = State::RequestState::Complete;
                    completions.emplace_back(std::move(request.second.completion),
                                             std::move(result));
                }
            }
        }
        for (auto &completion : completions)
        {
            if (completion.first)
            {
                completion.first(completion.second);
            }
        }
    }

    bool CommandRegistry::IsShutdown() const
    {
        const std::shared_ptr<State> state = state_;
        std::scoped_lock lock(state->mutex);
        return state->shutdown;
    }
}
