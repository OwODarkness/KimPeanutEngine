#include "command/command_local_transport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "command/command_agent_endpoint.h"

namespace kpengine::runtime::command
{
    namespace
    {
        constexpr std::chrono::milliseconds k_socket_poll_interval{100};

        std::string MakeTransportError(const char *message)
        {
            return std::string{"{\"status\":\"busy\",\"message\":\""} + message + "\"}";
        }

        bool WaitForReadable(const SOCKET socket)
        {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(socket, &read_set);
            TIMEVAL timeout{};
            timeout.tv_usec = static_cast<long>(k_socket_poll_interval.count() * 1000);
            return select(0, &read_set, nullptr, nullptr, &timeout) > 0;
        }

        bool SendLine(const SOCKET socket, const std::string &line)
        {
            const std::string framed = line + '\n';
            std::size_t sent = 0;
            while (sent < framed.size())
            {
                const int result = send(socket, framed.data() + sent,
                                        static_cast<int>(framed.size() - sent), 0);
                if (result == SOCKET_ERROR || result == 0)
                {
                    return false;
                }
                sent += static_cast<std::size_t>(result);
            }
            return true;
        }
    }

    struct CommandLocalTransport::State
    {
        struct Incoming { uint64_t id = 0; std::string line; };
        struct Outgoing { uint64_t id = 0; std::string line; };

        State(CommandRegistry &registry, LocalCommandTransportConfig initial_config)
            : endpoint(registry, initial_config.capabilities), config(std::move(initial_config)) {}

        void Run();
        void ServeClient(SOCKET client);
        bool EnqueueIncoming(std::string line, uint64_t &id);
        bool WaitForResponse(uint64_t id, std::string &line);
        void SetStartupResult(bool succeeded, std::string diagnostic);

        CommandAgentEndpoint endpoint;
        const LocalCommandTransportConfig config;
        std::atomic_bool running{false};
        std::thread worker;
        std::mutex queue_mutex;
        std::condition_variable response_cv;
        std::deque<Incoming> incoming;
        std::deque<Outgoing> outgoing;
        uint64_t next_message_id = 1;
        std::mutex startup_mutex;
        std::condition_variable startup_cv;
        bool startup_complete = false;
        bool startup_succeeded = false;
        std::string startup_diagnostic;
        std::atomic<uint16_t> bound_port{0};
    };

    CommandLocalTransport::CommandLocalTransport(CommandRegistry &registry,
                                                 LocalCommandTransportConfig config)
        : state_(std::make_unique<State>(registry, std::move(config))) {}

    CommandLocalTransport::~CommandLocalTransport() { Stop(); }

    bool CommandLocalTransport::Start(std::string &diagnostic)
    {
        State &state = *state_;
        if (!state.config.enabled)
        {
            diagnostic = "Local command transport is disabled";
            return false;
        }
        if (state.config.max_request_bytes == 0 || state.config.queue_capacity == 0 ||
            state.config.max_requests_per_tick == 0)
        {
            diagnostic = "Local command transport limits must be greater than zero";
            return false;
        }
        if (state.running.exchange(true))
        {
            diagnostic.clear();
            return true;
        }
        {
            std::scoped_lock lock(state.startup_mutex);
            state.startup_complete = false;
            state.startup_succeeded = false;
            state.startup_diagnostic.clear();
        }
        state.worker = std::thread(&State::Run, &state);
        std::unique_lock lock(state.startup_mutex);
        state.startup_cv.wait(lock, [&state] { return state.startup_complete; });
        diagnostic = state.startup_diagnostic;
        const bool succeeded = state.startup_succeeded;
        lock.unlock();
        if (!succeeded)
        {
            state.running.store(false);
            if (state.worker.joinable()) state.worker.join();
        }
        return succeeded;
    }

    void CommandLocalTransport::Stop()
    {
        State &state = *state_;
        state.running.store(false);
        state.response_cv.notify_all();
        if (state.worker.joinable()) state.worker.join();
        std::scoped_lock lock(state.queue_mutex);
        state.incoming.clear();
        state.outgoing.clear();
    }

    bool CommandLocalTransport::IsRunning() const noexcept { return state_->running.load(); }
    uint16_t CommandLocalTransport::BoundPort() const noexcept { return state_->bound_port.load(); }

    std::size_t CommandLocalTransport::PumpGameThread()
    {
        State &state = *state_;
        std::size_t processed = 0;
        while (processed < state.config.max_requests_per_tick)
        {
            State::Incoming request;
            {
                std::scoped_lock lock(state.queue_mutex);
                if (state.incoming.empty() || state.outgoing.size() >= state.config.queue_capacity) break;
                request = std::move(state.incoming.front());
                state.incoming.pop_front();
            }
            // Never hold a queue mutex while a command/provider runs.
            std::string response = state.endpoint.HandleJsonLine(request.line);
            {
                std::scoped_lock lock(state.queue_mutex);
                state.outgoing.push_back({request.id, std::move(response)});
            }
            state.response_cv.notify_one();
            ++processed;
        }
        return processed;
    }

    void CommandLocalTransport::State::SetStartupResult(const bool succeeded, std::string diagnostic)
    {
        {
            std::scoped_lock lock(startup_mutex);
            startup_succeeded = succeeded;
            startup_diagnostic = std::move(diagnostic);
            startup_complete = true;
        }
        startup_cv.notify_one();
    }

    void CommandLocalTransport::State::Run()
    {
        WSADATA winsock_data{};
        if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
        {
            SetStartupResult(false, "WSAStartup failed for local command transport");
            return;
        }
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            SetStartupResult(false, "Could not create local command transport socket");
            WSACleanup();
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(config.port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(listener, SOMAXCONN) == SOCKET_ERROR)
        {
            closesocket(listener);
            SetStartupResult(false, "Could not bind/listen on the local command transport port");
            WSACleanup();
            return;
        }
        sockaddr_in bound_address{};
        int bound_address_size = sizeof(bound_address);
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&bound_address), &bound_address_size) == SOCKET_ERROR)
        {
            closesocket(listener);
            SetStartupResult(false, "Could not query the local command transport port");
            WSACleanup();
            return;
        }
        bound_port.store(ntohs(bound_address.sin_port));
        SetStartupResult(true, {});
        while (running.load())
        {
            if (!WaitForReadable(listener)) continue;
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client != INVALID_SOCKET)
            {
                ServeClient(client);
                closesocket(client);
            }
        }
        closesocket(listener);
        bound_port.store(0);
        WSACleanup();
    }

    void CommandLocalTransport::State::ServeClient(const SOCKET client)
    {
        std::string buffered;
        char received[4096];
        while (running.load())
        {
            const std::size_t newline = buffered.find('\n');
            if (newline != std::string::npos)
            {
                std::string line = buffered.substr(0, newline);
                buffered.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.size() > config.max_request_bytes)
                {
                    if (!SendLine(client, MakeTransportError("request exceeds maximum size"))) return;
                    continue;
                }
                uint64_t id = 0;
                if (!EnqueueIncoming(std::move(line), id))
                {
                    if (!SendLine(client, MakeTransportError("agent request queue is full"))) return;
                    continue;
                }
                std::string response;
                if (!WaitForResponse(id, response) || !SendLine(client, response)) return;
                continue;
            }
            if (buffered.size() > config.max_request_bytes)
            {
                SendLine(client, MakeTransportError("request exceeds maximum size"));
                return;
            }
            if (!WaitForReadable(client)) continue;
            const int count = recv(client, received, static_cast<int>(sizeof(received)), 0);
            if (count <= 0) return;
            buffered.append(received, static_cast<std::size_t>(count));
        }
    }

    bool CommandLocalTransport::State::EnqueueIncoming(std::string line, uint64_t &id)
    {
        std::scoped_lock lock(queue_mutex);
        if (incoming.size() >= config.queue_capacity) return false;
        id = next_message_id++;
        incoming.push_back({id, std::move(line)});
        return true;
    }

    bool CommandLocalTransport::State::WaitForResponse(const uint64_t id, std::string &line)
    {
        std::unique_lock lock(queue_mutex);
        while (running.load())
        {
            const auto response = std::find_if(outgoing.begin(), outgoing.end(), [id](const Outgoing &item)
            { return item.id == id; });
            if (response != outgoing.end())
            {
                line = std::move(response->line);
                outgoing.erase(response);
                return true;
            }
            response_cv.wait_for(lock, k_socket_poll_interval);
        }
        return false;
    }
}
