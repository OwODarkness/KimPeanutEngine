#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <variant>

#include "command/command_registry.h"
#include "stats/performance_stats_command_provider.h"

namespace kpengine::runtime
{
    namespace
    {
        const command::CommandValue *Find(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr : &iterator->second;
        }
    }

    TEST(PerformanceStatsCommandProviderTest, RegistersThreeCommandsAndSupportsJsonFlag)
    {
        PerformanceStatsSnapshot expected{};
        expected.profile.frame_number = 42;
        expected.profile.graphics_api = GraphicsAPIType::GRAPHICS_API_VULKAN;
        expected.profile.draw_calls = 7;
        expected.profile.passes[static_cast<size_t>(render::RenderProfilePass::GBuffer)]
            .gpu_time_ms = 1.25;
        expected.frame_loop.frame_total_ms = 16.6;

        command::CommandRegistry registry;
        const auto registration = RegisterPerformanceStatsCommands(
            registry, [expected] { return expected; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const auto descriptors = registry.List();
        const auto is_stats_command = [](const command::CommandDesc &descriptor)
        {
            return descriptor.name == "gpu-stats" || descriptor.name == "cpu-stats" ||
                   descriptor.name == "stats";
        };
        EXPECT_EQ(std::count_if(descriptors.begin(), descriptors.end(), is_stats_command), 3);

        std::optional<command::CommandResult> completed;
        const command::CommandResult pending = registry.ExecuteText(
            "gpu-stats --json",
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [&completed](const command::CommandResult &result) { completed = result; });
        ASSERT_EQ(pending.status, command::CommandStatus::Pending) << pending.message;
        ASSERT_EQ(registry.PumpGameThread(), 1U);
        ASSERT_TRUE(completed.has_value());
        EXPECT_EQ(completed->status, command::CommandStatus::Success);
        ASSERT_NE(Find(completed->data, "format"), nullptr);
        EXPECT_EQ(std::get<std::string>(*Find(completed->data, "format")), "json");
        ASSERT_NE(Find(completed->data, "frame_number"), nullptr);
        EXPECT_EQ(std::get<uint64_t>(*Find(completed->data, "frame_number")), 42U);
        ASSERT_NE(Find(completed->data, "g_buffer_ms"), nullptr);
        EXPECT_DOUBLE_EQ(std::get<double>(*Find(completed->data, "g_buffer_ms")), 1.25);
    }
}
