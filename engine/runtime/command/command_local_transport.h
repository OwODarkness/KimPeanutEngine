#ifndef KPENGINE_RUNTIME_COMMAND_COMMAND_LOCAL_TRANSPORT_H
#define KPENGINE_RUNTIME_COMMAND_COMMAND_LOCAL_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "command/command_types.h"

namespace kpengine::runtime::command
{
    class CommandRegistry;

    struct LocalCommandTransportConfig
    {
        bool enabled = false;
        uint16_t port = 37373;
        std::size_t max_request_bytes = 64U * 1024U;
        std::size_t queue_capacity = 128U;
        std::size_t max_requests_per_tick = 32U;
        CommandCapability capabilities = CommandCapability::None;
    };

    // Socket I/O never enters the command registry. Engine::GameTick() calls
    // PumpGameThread(), which is the only handler-execution boundary.
    class CommandLocalTransport final
    {
    public:
        CommandLocalTransport(CommandRegistry &registry, LocalCommandTransportConfig config);
        ~CommandLocalTransport();

        CommandLocalTransport(const CommandLocalTransport &) = delete;
        CommandLocalTransport &operator=(const CommandLocalTransport &) = delete;

        bool Start(std::string &diagnostic);
        void Stop();

        bool IsRunning() const noexcept;
        uint16_t BoundPort() const noexcept;
        std::size_t PumpGameThread();

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}

#endif
