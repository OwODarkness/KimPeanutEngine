#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "command/command_local_transport.h"
#include "command/command_registry.h"

namespace
{
    using kpengine::runtime::command::CommandCall;
    using kpengine::runtime::command::CommandCategory;
    using kpengine::runtime::command::CommandDesc;
    using kpengine::runtime::command::CommandFlags;
    using kpengine::runtime::command::CommandLocalTransport;
    using kpengine::runtime::command::CommandStatus;
    using kpengine::runtime::command::CommandThread;
    using kpengine::runtime::command::LocalCommandTransportConfig;

    std::string SendRequest(const uint16_t port, const std::string &request)
    {
        SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_handle == INVALID_SOCKET)
        {
            return {};
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(socket_handle, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(socket_handle);
            return {};
        }
        const std::string framed = request + '\n';
        if (send(socket_handle, framed.data(), static_cast<int>(framed.size()), 0) == SOCKET_ERROR)
        {
            closesocket(socket_handle);
            return {};
        }

        std::string response;
        char buffer[512];
        while (response.find('\n') == std::string::npos)
        {
            const int count = recv(socket_handle, buffer, static_cast<int>(sizeof(buffer)), 0);
            if (count <= 0)
            {
                response.clear();
                break;
            }
            response.append(buffer, static_cast<std::size_t>(count));
        }
        closesocket(socket_handle);
        return response;
    }
}

TEST(CommandLocalTransportTest, DispatchesAgentRequestsOnlyWhenPumpedByGameThread)
{
    kpengine::runtime::command::CommandRegistry registry;
    std::atomic_int execution_count{0};
    const auto registration = registry.Register(CommandDesc{
        "test.live_game", "LocalTransportTest", "", CommandCategory::Test,
        CommandFlags::AgentAllowed, {},
        [&execution_count](const CommandCall &, const auto &)
        {
            ++execution_count;
            return kpengine::runtime::command::CommandResult{CommandStatus::Success, "done", 0, {}};
        },
        CommandThread::Game});
    ASSERT_TRUE(registration.IsSuccess());

    LocalCommandTransportConfig config{};
    config.enabled = true;
    config.port = 0;
    CommandLocalTransport transport{registry, config};
    std::string diagnostic;
    ASSERT_TRUE(transport.Start(diagnostic)) << diagnostic;
    ASSERT_NE(transport.BoundPort(), 0U);

    auto response = std::async(std::launch::async, SendRequest, transport.BoundPort(),
                               "{\"op\":\"execute\",\"command\":\"test.live_game\"}");
    for (int attempt = 0; attempt < 100 && response.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready;
         ++attempt)
    {
        transport.PumpGameThread();
        registry.PumpGameThread();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }

    ASSERT_EQ(response.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_NE(response.get().find("\"status\":\"pending\""), std::string::npos);
    EXPECT_EQ(execution_count.load(), 1);
    transport.Stop();
}
