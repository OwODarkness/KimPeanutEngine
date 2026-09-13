#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "command/command_registry.h"

namespace
{
    using kpengine::runtime::command::CommandCategory;
    using kpengine::runtime::command::CommandContext;
    using kpengine::runtime::command::CommandDesc;
    using kpengine::runtime::command::CommandRegistry;
    using kpengine::runtime::command::CommandResult;
    using kpengine::runtime::command::CommandStatus;

    CommandDesc MakeDescriptor(const std::string &name)
    {
        return {name,
                "CompletionTest",
                {},
                CommandCategory::Test,
                {},
                {},
                [](const kpengine::runtime::command::CommandCall &,
                   const CommandContext &)
                { return CommandResult{CommandStatus::Success, {}, 0, {}}; }};
    }
}

TEST(CommandRegistryCompletionTest, CompletesBuiltInHelpCommand)
{
    CommandRegistry registry;
    EXPECT_EQ(registry.CompleteCommandNames("hel"),
              (std::vector<std::string>{"help"}));
}

TEST(CommandRegistryCompletionTest, UsesTrieForSortedPrefixCandidates)
{
    CommandRegistry registry;
    const auto alpha = registry.Register(MakeDescriptor("test.alpha"));
    const auto beta = registry.Register(MakeDescriptor("test.beta"));
    const auto other = registry.Register(MakeDescriptor("other.command"));
    ASSERT_TRUE(alpha.IsSuccess());
    ASSERT_TRUE(beta.IsSuccess());
    ASSERT_TRUE(other.IsSuccess());

    EXPECT_EQ(registry.CompleteCommandNames("test."),
              (std::vector<std::string>{"test.alpha", "test.beta"}));
    EXPECT_EQ(registry.CompleteCommandNames("test.", 1),
              (std::vector<std::string>{"test.alpha"}));
    EXPECT_TRUE(registry.CompleteCommandNames("missing").empty());
}

TEST(CommandRegistryCompletionTest, TracksRegistrationLifetimeAndShutdown)
{
    CommandRegistry registry;
    {
        const auto registration = registry.Register(MakeDescriptor("test.temporary"));
        ASSERT_TRUE(registration.IsSuccess());
        EXPECT_EQ(registry.CompleteCommandNames("test.temp"),
                  (std::vector<std::string>{"test.temporary"}));
    }

    EXPECT_TRUE(registry.CompleteCommandNames("test.temp").empty());
    registry.Shutdown();
    EXPECT_TRUE(registry.CompleteCommandNames({}).empty());
}
