#include "window_command_provider.h"

#include <utility>

#include "window_system.h"

namespace kpengine::runtime
{
    namespace
    {
        // Matches launch_options' --resize bound, so a runtime command cannot ask
        // for an extent the launch-time path would have rejected.
        constexpr uint64_t kMinimumExtent = 1;
        constexpr uint64_t kMaximumExtent = 16384;

        // Declared UnsignedInteger on purpose: an extent cannot be negative, and
        // the JSON transport parses every non-negative literal as an unsigned
        // integer, so a SignedInteger schema argument would reject `1024`.
        bool ResolveExtent(const command::CommandArguments &arguments,
                           const char *name,
                           int &out)
        {
            const auto iterator = arguments.find(name);
            if (iterator == arguments.end())
            {
                return false;
            }
            const auto *value = std::get_if<uint64_t>(&iterator->second);
            if (value == nullptr || *value < kMinimumExtent || *value > kMaximumExtent)
            {
                return false;
            }
            out = static_cast<int>(*value);
            return true;
        }
    }

    command::CommandRegistrationResult RegisterWindowCommands(
        command::CommandRegistry &registry,
        std::function<WindowSystem *()> window_resolver)
    {
        if (!window_resolver)
        {
            return {{}, command::CommandRegistrationStatus::InvalidDescriptor,
                    "Window command provider requires a window resolver"};
        }

        command::CommandDesc descriptor{
            "window.resize",
            "RuntimeWindow",
            "Resize the active window's client area",
            command::CommandCategory::Engine,
            command::CommandFlags::AgentAllowed | command::CommandFlags::LuaAllowed |
                command::CommandFlags::MutatesState,
            {{command::CommandArgumentDesc{"width", command::CommandValueType::UnsignedInteger,
                                           true, {}, {}},
              command::CommandArgumentDesc{"height", command::CommandValueType::UnsignedInteger,
                                           true, {}, {}}}},
            [resolver = std::move(window_resolver)](
                const command::CommandCall &call, const command::CommandContext &context)
            {
                int width = 0;
                int height = 0;
                if (!ResolveExtent(call.arguments, "width", width) ||
                    !ResolveExtent(call.arguments, "height", height))
                {
                    return command::CommandResult{
                        command::CommandStatus::InvalidArguments,
                        "window.resize requires integer width and height from 1 to " +
                            std::to_string(kMaximumExtent),
                        context.request_id,
                        {}};
                }

                // Resolved per call: a host may build its window on the render
                // thread after this command provider was registered.
                WindowSystem *window = resolver();
                if (window == nullptr)
                {
                    return command::CommandResult{
                        command::CommandStatus::Failed,
                        "The current mode has no window to resize",
                        context.request_id,
                        {}};
                }

                // Recorded rather than applied: the command runs on the game
                // thread and a real window may only be touched on the window
                // thread, which applies the request at its next frame boundary.
                window->QueueWindowSizeRequest(width, height);

                int recorded_width = 0;
                int recorded_height = 0;
                window->GetRecordedWindowSize(recorded_width, recorded_height);

                return command::CommandResult{
                    command::CommandStatus::Success,
                    "Window resize requested",
                    context.request_id,
                    {{"width", static_cast<uint64_t>(width)},
                     {"height", static_cast<uint64_t>(height)},
                     {"recorded_width", static_cast<uint64_t>(recorded_width)},
                     {"recorded_height", static_cast<uint64_t>(recorded_height)},
                     {"applied", false}}};
            },
            command::CommandThread::Game};

        return registry.Register(std::move(descriptor));
    }
}
