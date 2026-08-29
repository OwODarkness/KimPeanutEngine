#include <gtest/gtest.h>

#include <utility>

#include "command/command_registry.h"

namespace
{
    using kpengine::runtime::command::CommandCall;
    using kpengine::runtime::command::CommandCategory;
    using kpengine::runtime::command::CommandContext;
    using kpengine::runtime::command::CommandOrigin;
    using kpengine::runtime::command::CommandRegistry;
    using kpengine::runtime::command::CommandResult;
    using kpengine::runtime::command::CommandStatus;
}

TEST(CommandRegistryTest, RegistersFindsExecutesAndUnregisters)
{
    CommandRegistry registry;
    int execution_count = 0;
    const auto registration = registry.Register({
        "test.echo",
        "TestCommandProvider",
        "Return a test result",
        CommandCategory::Test,
        {},
        {},
        [&execution_count](const CommandCall &call, const CommandContext &context)
        {
            ++execution_count;
            EXPECT_EQ(call.name, "test.echo");
            EXPECT_EQ(context.origin, CommandOrigin::Test);
            return CommandResult{CommandStatus::Success, "ok", 0, {}};
        },
    });

    ASSERT_TRUE(registration.IsSuccess());
    EXPECT_EQ(registration.registration.IsValid(), true);
    ASSERT_TRUE(registry.Find("test.echo").has_value());
    const CommandResult result = registry.Execute({"test.echo", {}}, {CommandOrigin::Test, {}});
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(execution_count, 1);

    // Registration is owned by the scoped token and is removed at scope exit.
}

TEST(CommandRegistryTest, RegistrationTokenControlsLifetime)
{
    CommandRegistry registry;
    {
        const auto registration = registry.Register({
            "test.scoped",
            "TestCommandProvider",
            "",
            CommandCategory::Test,
            {},
            {},
            [](const CommandCall &, const CommandContext &)
            { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
        });
        ASSERT_TRUE(registration.IsSuccess());
        EXPECT_TRUE(registry.Find("test.scoped").has_value());
    }

    EXPECT_FALSE(registry.Find("test.scoped").has_value());
    EXPECT_EQ(registry.Execute({"test.scoped", {}}, {}).status, CommandStatus::NotFound);
}

TEST(CommandRegistryTest, RegistrationTokenMovesOwnership)
{
    CommandRegistry registry;
    auto first = registry.Register({
        "test.move", "TestProvider", "", CommandCategory::Test, {}, {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });
    ASSERT_TRUE(first.IsSuccess());

    auto second = std::move(first.registration);
    EXPECT_FALSE(first.registration.IsValid());
    EXPECT_TRUE(second.IsValid());
    EXPECT_TRUE(registry.Find("test.move").has_value());
}

TEST(CommandRegistryTest, RejectsMalformedAndDuplicateRegistrations)
{
    CommandRegistry registry;
    EXPECT_EQ(registry.Register({}).status,
              kpengine::runtime::command::CommandRegistrationStatus::InvalidDescriptor);

    const auto first = registry.Register({
        "test.duplicate",
        "FirstProvider",
        "",
        CommandCategory::Test,
        {},
        {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });
    ASSERT_TRUE(first.IsSuccess());
    const auto duplicate = registry.Register({
        "test.duplicate",
        "SecondProvider",
        "",
        CommandCategory::Test,
        {},
        {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });
    EXPECT_EQ(duplicate.status,
              kpengine::runtime::command::CommandRegistrationStatus::DuplicateName);
    EXPECT_NE(duplicate.diagnostic.find("FirstProvider"), std::string::npos);
}

TEST(CommandRegistryTest, ListsCommandsDeterministically)
{
    CommandRegistry registry;
    const auto first = registry.Register({
        "test.zeta", "TestProvider", "", CommandCategory::Test, {}, {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });
    const auto second = registry.Register({
        "test.alpha", "TestProvider", "", CommandCategory::Test, {}, {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });
    ASSERT_TRUE(first.IsSuccess());
    ASSERT_TRUE(second.IsSuccess());

    const auto commands = registry.List();
    ASSERT_EQ(commands.size(), 4U);
    EXPECT_EQ(commands[0].name, "commands.list");
    EXPECT_EQ(commands[1].name, "help");
    EXPECT_EQ(commands[2].name, "test.alpha");
    EXPECT_EQ(commands[3].name, "test.zeta");
}

TEST(CommandRegistryTest, BuiltinHelpAndListCommandsAreStructured)
{
    CommandRegistry registry;
    const auto registration = registry.Register({
        "test.help_target", "TestProvider", "Target help", CommandCategory::Test, {}, {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });
    ASSERT_TRUE(registration.IsSuccess());

    const CommandResult list = registry.Execute({"commands.list", {}}, {});
    ASSERT_TRUE(list.IsSuccess());
    EXPECT_NE(list.message.find("test.help_target"), std::string::npos);
    EXPECT_TRUE(std::holds_alternative<uint64_t>(list.data.at("count")));

    const CommandResult help = registry.Execute(
        {"help", {{"name", std::string{"test.help_target"}}}}, {});
    ASSERT_TRUE(help.IsSuccess());
    EXPECT_NE(help.message.find("Target help"), std::string::npos);
    EXPECT_NE(help.message.find("Usage: test.help_target"), std::string::npos);
    EXPECT_EQ(std::get<std::string>(help.data.at("provider")), "TestProvider");
}

TEST(CommandRegistryTest, ShutdownClearsCommandsAndRejectsFutureWork)
{
    CommandRegistry registry;
    const auto registration = registry.Register({
        "test.shutdown", "TestProvider", "", CommandCategory::Test, {}, {},
        [](const CommandCall &, const CommandContext &)
        { return CommandResult{CommandStatus::Pending, "pending", 17, {}}; },
    });
    ASSERT_TRUE(registration.IsSuccess());

    registry.Shutdown();
    EXPECT_TRUE(registry.IsShutdown());
    EXPECT_FALSE(registry.Find("test.shutdown").has_value());
    EXPECT_EQ(registry.Execute({"test.shutdown", {}}, {}).status, CommandStatus::Shutdown);
    EXPECT_EQ(registry.Register({
                  "test.after_shutdown", "TestProvider", "", CommandCategory::Test, {}, {},
                  [](const CommandCall &, const CommandContext &)
                  { return CommandResult{CommandStatus::Success, {}, 0, {}}; },
              }).status,
              kpengine::runtime::command::CommandRegistrationStatus::Shutdown);
}
