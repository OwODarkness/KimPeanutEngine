#ifndef KPENGINE_RUNTIME_COMMAND_COMMAND_REGISTRY_H
#define KPENGINE_RUNTIME_COMMAND_COMMAND_REGISTRY_H

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_types.h"

namespace kpengine::runtime::command
{
    using CommandCompletionHandler = std::function<void(const CommandResult &)>;

    class CommandRegistration final
    {
    public:
        CommandRegistration() = default;
        ~CommandRegistration();

        CommandRegistration(const CommandRegistration &) = delete;
        CommandRegistration &operator=(const CommandRegistration &) = delete;
        CommandRegistration(CommandRegistration &&other) noexcept;
        CommandRegistration &operator=(CommandRegistration &&other) noexcept;

        bool IsValid() const noexcept { return static_cast<bool>(release_); }

    private:
        friend class CommandRegistry;

        explicit CommandRegistration(std::function<void()> release) : release_(std::move(release)) {}

        std::function<void()> release_;
    };

    enum class CommandRegistrationStatus : uint8_t
    {
        Registered,
        InvalidDescriptor,
        InvalidSchema,
        DuplicateName,
        Shutdown,
    };

    struct CommandRegistrationResult
    {
        CommandRegistration registration;
        CommandRegistrationStatus status = CommandRegistrationStatus::InvalidDescriptor;
        std::string diagnostic;

        bool IsSuccess() const noexcept
        {
            return status == CommandRegistrationStatus::Registered && registration.IsValid();
        }
    };

    class CommandRegistry final
    {
    public:
        CommandRegistry();
        ~CommandRegistry();

        CommandRegistry(const CommandRegistry &) = delete;
        CommandRegistry &operator=(const CommandRegistry &) = delete;
        CommandRegistry(CommandRegistry &&) = delete;
        CommandRegistry &operator=(CommandRegistry &&) = delete;

        // A successful result owns the registration lifetime through its token.
        // Failure includes a diagnostic and never installs a partial entry.
        CommandRegistrationResult Register(CommandDesc descriptor);

        std::optional<CommandDesc> Find(const std::string &name) const;
        std::vector<CommandDesc> List() const;
        CommandResult Execute(const CommandCall &call, const CommandContext &context,
                              CommandCompletionHandler completion = {}) const;
        CommandResult ExecuteText(std::string_view text, const CommandContext &context,
                                  CommandCompletionHandler completion = {}) const;

        // Executes queued Game-lane requests. The caller must be the Runtime
        // game thread; the lane is stamped into the handler context.
        std::size_t PumpGameThread(
            std::size_t max_commands = std::numeric_limits<std::size_t>::max());

        // Returns and removes a terminal result for a deferred request. A
        // completion callback, when supplied, is delivered exactly once before
        // this result becomes available here.
        std::optional<CommandResult> TakeCompletion(uint64_t request_id);

        // Terminally cancels a registry-owned request. This does not forcibly
        // interrupt subsystem work already handed off by a Pending handler.
        bool CancelRequest(uint64_t request_id);

        std::size_t PendingRequestCount() const;

        // Clears all registrations and rejects later registration/execution.
        // Providers must cancel work represented by Pending results before or
        // during their own shutdown; the registry does not own those operations.
        void Shutdown();
        bool IsShutdown() const;

    private:
        struct State;

        static CommandResult MakeCommandListResult(const std::shared_ptr<State> &state);
        static CommandResult MakeHelpResult(const std::shared_ptr<State> &state,
                                             const CommandCall &call);
        static void InstallBuiltins(const std::shared_ptr<State> &state);

        static CommandResult ExecuteResolved(const std::shared_ptr<State> &state,
                                              CommandDesc descriptor,
                                              uint64_t registration_id,
                                              const CommandCall &call,
                                              const CommandContext &context,
                                              CommandCompletionHandler completion);
        static CommandResult EnqueueGameRequest(const std::shared_ptr<State> &state,
                                                 CommandCall call,
                                                 CommandContext context,
                                                 CommandDesc descriptor,
                                                 uint64_t registration_id,
                                                 CommandCompletionHandler completion);
        static void CompleteRequest(const std::shared_ptr<State> &state,
                                     uint64_t request_id,
                                     CommandResult result);

        std::shared_ptr<State> state_;
    };
}

#endif
