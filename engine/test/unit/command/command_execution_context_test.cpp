#include <gtest/gtest.h>

#include <vector>

#include "command/command_registry.h"

namespace
{
    using kpengine::runtime::command::CommandCall;
    using kpengine::runtime::command::CommandCapability;
    using kpengine::runtime::command::CommandCategory;
    using kpengine::runtime::command::CommandContext;
    using kpengine::runtime::command::CommandDesc;
    using kpengine::runtime::command::CommandFlags;
    using kpengine::runtime::command::CommandOrigin;
    using kpengine::runtime::command::CommandRegistry;
    using kpengine::runtime::command::CommandResult;
    using kpengine::runtime::command::CommandStatus;
    using kpengine::runtime::command::CommandThread;

    CommandDesc MakeCommand(const char *name, int *execution_count,
                            const CommandThread expected_thread = CommandThread::Immediate)
    {
        return {
            name,
            "ContextTest",
            "Context test command",
            CommandCategory::Test,
            CommandFlags::None,
            {},
            [execution_count, expected_thread](const CommandCall &,
                              const CommandContext &context)
            {
                ++*execution_count;
                EXPECT_EQ(context.thread, expected_thread);
                return CommandResult{CommandStatus::Success, "done", 0, {}};
            },
        };
    }
}

TEST(CommandExecutionContextTest, QueuesGameCommandsAndCompletesExactlyOnce)
{
    CommandRegistry registry;
    int execution_count = 0;
    CommandDesc descriptor = MakeCommand("test.game", &execution_count, CommandThread::Game);
    descriptor.execution_thread = CommandThread::Game;
    const auto registration = registry.Register(std::move(descriptor));
    ASSERT_TRUE(registration.IsSuccess());

    std::vector<CommandResult> callbacks;
    const CommandResult pending = registry.Execute(
        {"test.game", {}}, {CommandOrigin::Test, CommandThread::Immediate},
        [&callbacks](const CommandResult &result) { callbacks.push_back(result); });
    ASSERT_EQ(pending.status, CommandStatus::Pending);
    ASSERT_NE(pending.request_id, 0U);
    EXPECT_EQ(registry.PendingRequestCount(), 1U);
    EXPECT_FALSE(registry.TakeCompletion(pending.request_id).has_value());
    EXPECT_EQ(execution_count, 0);

    EXPECT_EQ(registry.PumpGameThread(), 1U);
    EXPECT_EQ(execution_count, 1);
    ASSERT_EQ(callbacks.size(), 1U);
    EXPECT_EQ(callbacks.front().status, CommandStatus::Success);
    EXPECT_EQ(callbacks.front().request_id, pending.request_id);
    EXPECT_EQ(registry.PendingRequestCount(), 0U);

    const auto completion = registry.TakeCompletion(pending.request_id);
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->status, CommandStatus::Success);
    EXPECT_EQ(completion->request_id, pending.request_id);
    EXPECT_FALSE(registry.TakeCompletion(pending.request_id).has_value());
    EXPECT_EQ(registry.PumpGameThread(), 0U);
}

TEST(CommandExecutionContextTest, RejectsWrongLaneWithoutInvokingHandler)
{
    CommandRegistry registry;
    int execution_count = 0;
    CommandDesc descriptor = MakeCommand("test.render", &execution_count, CommandThread::Render);
    descriptor.execution_thread = CommandThread::Render;
    const auto registration = registry.Register(std::move(descriptor));
    ASSERT_TRUE(registration.IsSuccess());

    const CommandResult wrong_lane = registry.Execute(
        {"test.render", {}}, {CommandOrigin::Test, CommandThread::Game});
    EXPECT_EQ(wrong_lane.status, CommandStatus::WrongThread);
    EXPECT_EQ(execution_count, 0);

    const CommandResult correct_lane = registry.Execute(
        {"test.render", {}}, {CommandOrigin::Test, CommandThread::Render});
    EXPECT_EQ(correct_lane.status, CommandStatus::Success);
    EXPECT_EQ(execution_count, 1);
}

TEST(CommandExecutionContextTest, EnforcesOriginFlagsAndCapabilities)
{
    CommandRegistry registry;
    int execution_count = 0;
    CommandDesc descriptor = MakeCommand("test.protected", &execution_count);
    descriptor.flags = CommandFlags::DevelopmentOnly | CommandFlags::AgentAllowed |
                       CommandFlags::LuaAllowed | CommandFlags::MutatesState |
                       CommandFlags::Destructive;
    const auto registration = registry.Register(std::move(descriptor));
    ASSERT_TRUE(registration.IsSuccess());

    const CommandContext agent{CommandOrigin::Agent, CommandThread::Immediate,
                               CommandCapability::None};
    EXPECT_EQ(registry.Execute({"test.protected", {}}, agent).status,
              CommandStatus::Denied);

    const CommandContext development{CommandOrigin::Agent, CommandThread::Immediate,
                                     CommandCapability::Development};
    EXPECT_EQ(registry.Execute({"test.protected", {}}, development).status,
              CommandStatus::Denied);

    const CommandContext authorized{
        CommandOrigin::Agent,
        CommandThread::Immediate,
        CommandCapability::Development | CommandCapability::Mutating |
            CommandCapability::Destructive};
    EXPECT_EQ(registry.Execute({"test.protected", {}}, authorized).status,
              CommandStatus::Success);
    EXPECT_EQ(execution_count, 1);

    const CommandContext lua_without_flag{
        CommandOrigin::Lua, CommandThread::Immediate,
        CommandCapability::Development | CommandCapability::Mutating |
            CommandCapability::Destructive};
    // This command has LuaAllowed; the same capability set is accepted.
    EXPECT_EQ(registry.Execute({"test.protected", {}}, lua_without_flag).status,
              CommandStatus::Success);
    EXPECT_EQ(execution_count, 2);
}

TEST(CommandExecutionContextTest, ShutdownCancelsQueuedRequestsOnce)
{
    CommandRegistry registry;
    int execution_count = 0;
    CommandDesc descriptor = MakeCommand("test.shutdown_queue", &execution_count);
    descriptor.execution_thread = CommandThread::Game;
    const auto registration = registry.Register(std::move(descriptor));
    ASSERT_TRUE(registration.IsSuccess());

    int callback_count = 0;
    CommandResult callback_result{};
    const CommandResult pending = registry.Execute(
        {"test.shutdown_queue", {}}, {CommandOrigin::Test, CommandThread::Immediate},
        [&callback_count, &callback_result](const CommandResult &result)
        {
            ++callback_count;
            callback_result = result;
        });
    ASSERT_EQ(pending.status, CommandStatus::Pending);

    registry.Shutdown();
    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_result.status, CommandStatus::Shutdown);
    EXPECT_EQ(callback_result.request_id, pending.request_id);
    EXPECT_EQ(registry.PendingRequestCount(), 0U);
    EXPECT_EQ(registry.PumpGameThread(), 0U);
    EXPECT_EQ(execution_count, 0);

    const auto completion = registry.TakeCompletion(pending.request_id);
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->status, CommandStatus::Shutdown);
    EXPECT_FALSE(registry.TakeCompletion(pending.request_id).has_value());
}
