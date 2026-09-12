#include <functional>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "command/command_registry.h"
#include "window/window_command_provider.h"
#include "window/window_system.h"

namespace kpengine::runtime
{
    namespace
    {
        // Records what the platform would have been asked for. Everything else
        // is stubbed: this test is about the command's contract, not the window.
        class FakeWindowSystem final : public WindowSystem
        {
        public:
            int requested_width = 0;
            int requested_height = 0;
            int request_count = 0;

            bool Initialize(const WindowCreateInfo &) override { return true; }
            void PollEvents() override {}
            void SwapBuffers() override {}
            WindowCaptureResult CaptureWindow() override { return {}; }
            WindowHandle GetNativeHandle() const override { return nullptr; }
            bool ShouldClose() const override { return false; }
            void SetMouseCapture(bool) override {}
            bool IsMouseCaptured() const override { return false; }
            void Cleanup() override {}

            void RequestWindowSize(int width, int height) override
            {
                requested_width = width;
                requested_height = height;
                ++request_count;
                WindowSystem::SetWindowSize(width, height);
            }
        };

        const uint64_t *GetInteger(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr : std::get_if<uint64_t>(&iterator->second);
        }

        // Extents travel as unsigned because the JSON transport turns every
        // non-negative literal into an unsigned integer; a signed schema
        // argument would reject `{"width": 1024}`.
        command::CommandArguments Extent(const uint64_t width, const uint64_t height)
        {
            return {{"width", width}, {"height", height}};
        }

        const bool *GetBoolean(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr : std::get_if<bool>(&iterator->second);
        }

        // Dispatches from the Test origin the way the agent transport does: the
        // handler is a Game-lane command, so the request is queued and drained
        // by PumpGameThread.
        command::CommandResult ExecuteAndPump(command::CommandRegistry &registry,
                                              command::CommandCall call,
                                              const command::CommandCapability capabilities)
        {
            const command::CommandResult pending = registry.Execute(
                std::move(call),
                {command::CommandOrigin::Agent, command::CommandThread::Immediate,
                 capabilities});
            if (pending.status != command::CommandStatus::Pending)
            {
                return pending;
            }
            if (registry.PumpGameThread() != 1U)
            {
                return {command::CommandStatus::Failed, "game lane did not run", 0, {}};
            }
            const std::optional<command::CommandResult> completion =
                registry.TakeCompletion(pending.request_id);
            return completion.has_value()
                       ? *completion
                       : command::CommandResult{command::CommandStatus::Failed,
                                                "no completion recorded", 0, {}};
        }
    }

    TEST(WindowCommandProviderTest, QueuesResizeAndAppliesItAtTheWindowBoundary)
    {
        FakeWindowSystem window;
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterWindowCommands(registry, [&window]() -> WindowSystem * { return &window; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(
            registry,
            {"window.resize", Extent(1440u, 900u)},
            command::CommandCapability::Mutating);

        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        ASSERT_NE(GetInteger(result.data, "width"), nullptr);
        EXPECT_EQ(*GetInteger(result.data, "width"), 1440);
        ASSERT_NE(GetInteger(result.data, "height"), nullptr);
        EXPECT_EQ(*GetInteger(result.data, "height"), 900);
        // The command only queues. Nothing has touched the window yet, and the
        // result reports the size the window currently records so a caller can
        // see the request has not landed.
        ASSERT_NE(GetBoolean(result.data, "applied"), nullptr);
        EXPECT_FALSE(*GetBoolean(result.data, "applied"));
        ASSERT_NE(GetInteger(result.data, "recorded_width"), nullptr);
        EXPECT_EQ(*GetInteger(result.data, "recorded_width"), 0);
        EXPECT_EQ(window.request_count, 0);

        EXPECT_TRUE(window.ApplyQueuedWindowSizeRequest());
        EXPECT_EQ(window.request_count, 1);
        EXPECT_EQ(window.requested_width, 1440);
        EXPECT_EQ(window.requested_height, 900);
    }

    TEST(WindowCommandProviderTest, KeepsOnlyTheNewestUnappliedResize)
    {
        FakeWindowSystem window;
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterWindowCommands(registry, [&window]() -> WindowSystem * { return &window; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        ASSERT_EQ(ExecuteAndPump(
                      registry,
                      {"window.resize", Extent(640u, 480u)},
                      command::CommandCapability::Mutating)
                      .status,
                  command::CommandStatus::Success);
        ASSERT_EQ(ExecuteAndPump(
                      registry,
                      {"window.resize", Extent(1024u, 768u)},
                      command::CommandCapability::Mutating)
                      .status,
                  command::CommandStatus::Success);

        EXPECT_TRUE(window.ApplyQueuedWindowSizeRequest());
        EXPECT_EQ(window.request_count, 1);
        EXPECT_EQ(window.requested_width, 1024);
        EXPECT_EQ(window.requested_height, 768);
        EXPECT_FALSE(window.ApplyQueuedWindowSizeRequest());
    }

    TEST(WindowCommandProviderTest, RejectsExtentsOutsideTheLaunchTimeBound)
    {
        FakeWindowSystem window;
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterWindowCommands(registry, [&window]() -> WindowSystem * { return &window; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const command::CommandCall calls[] = {
            {"window.resize", Extent(0u, 720u)},
            // Negative extents cannot be expressed as an unsigned argument, so a
            // wire client reaches this arm through the schema check instead.
            {"window.resize", {{"width", std::string{"-4"}}, {"height", uint64_t{720}}}},
            {"window.resize", Extent(640u, 16385u)},
            {"window.resize", {{"width", uint64_t{640}}}},
            {"window.resize", {{"width", std::string{"640"}}, {"height", uint64_t{480}}}},
        };
        for (const command::CommandCall &call : calls)
        {
            const command::CommandResult result = ExecuteAndPump(
                registry, call, command::CommandCapability::Mutating);
            EXPECT_EQ(result.status, command::CommandStatus::InvalidArguments)
                << result.message;
        }
        EXPECT_EQ(window.request_count, 0);
        EXPECT_FALSE(window.ApplyQueuedWindowSizeRequest());
    }

    TEST(WindowCommandProviderTest, ReportsFailureWhenTheModeHasNoWindow)
    {
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterWindowCommands(registry, []() -> WindowSystem * { return nullptr; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(
            registry,
            {"window.resize", Extent(1280u, 720u)},
            command::CommandCapability::Mutating);
        EXPECT_EQ(result.status, command::CommandStatus::Failed);
        EXPECT_FALSE(result.message.empty());
    }

    TEST(WindowCommandProviderTest, RequiresTheMutatingCapability)
    {
        FakeWindowSystem window;
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterWindowCommands(registry, [&window]() -> WindowSystem * { return &window; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const std::optional<command::CommandDesc> descriptor = registry.Find("window.resize");
        ASSERT_TRUE(descriptor.has_value());
        EXPECT_TRUE(command::HasCommandFlag(descriptor->flags,
                                           command::CommandFlags::MutatesState));
        EXPECT_TRUE(command::HasCommandFlag(descriptor->flags,
                                           command::CommandFlags::AgentAllowed));
        // Resizing the client area moves the frame boundary the renderer
        // submits against, so it must run on the game lane like every other
        // mutating Runtime command.
        EXPECT_EQ(descriptor->execution_thread, command::CommandThread::Game);
        // An agent sends `{"width": 1024}`, which the JSON transport parses as an
        // unsigned integer. Declaring the extents signed would reject that.
        ASSERT_EQ(descriptor->schema.arguments.size(), 2U);
        for (const command::CommandArgumentDesc &argument : descriptor->schema.arguments)
        {
            EXPECT_TRUE(argument.required) << argument.name;
            EXPECT_EQ(argument.type, command::CommandValueType::UnsignedInteger)
                << argument.name;
        }

        // A transport that did not opt into mutating commands cannot reach it.
        const command::CommandResult denied = registry.Execute(
            {"window.resize", Extent(800u, 600u)},
            {command::CommandOrigin::Agent, command::CommandThread::Immediate,
             command::CommandCapability::None});
        EXPECT_EQ(denied.status, command::CommandStatus::Denied);
        EXPECT_EQ(registry.PendingRequestCount(), 0U);
        EXPECT_EQ(window.request_count, 0);
    }

    TEST(WindowCommandProviderTest, RejectsAMissingResolver)
    {
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterWindowCommands(registry, std::function<WindowSystem *()>{});
        EXPECT_FALSE(registration.IsSuccess());
        EXPECT_FALSE(registration.diagnostic.empty());
        EXPECT_FALSE(registry.Find("window.resize").has_value());
    }
}
