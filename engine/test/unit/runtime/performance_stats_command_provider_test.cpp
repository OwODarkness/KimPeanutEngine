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
        expected.profile.passes[static_cast<size_t>(render::RenderProfilePass::GBuffer)]
            .cpu_time_ms = 0.75;
        expected.profile.summary.passes[static_cast<size_t>(render::RenderProfilePass::GBuffer)]
            .gpu_p95_ms = 2.5;
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
        ASSERT_NE(Find(completed->data, "pass.g_buffer.gpu_p95_ms"), nullptr);
        EXPECT_DOUBLE_EQ(std::get<double>(*Find(completed->data, "pass.g_buffer.gpu_p95_ms")), 2.5);

        std::optional<command::CommandResult> combined;
        const command::CommandResult combined_pending = registry.ExecuteText(
            "stats --json",
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [&combined](const command::CommandResult &result) { combined = result; });
        ASSERT_EQ(combined_pending.status, command::CommandStatus::Pending)
            << combined_pending.message;
        ASSERT_EQ(registry.PumpGameThread(), 1U);
        ASSERT_TRUE(combined.has_value());
        ASSERT_NE(Find(combined->data, "pass.g_buffer.cpu_ms"), nullptr);
        EXPECT_DOUBLE_EQ(std::get<double>(*Find(combined->data, "pass.g_buffer.cpu_ms")), 0.75);
    }

    // The profiler reports no window log, so every stats command has to carry
    // enough state for a caller to know whether the percentiles mean anything.
    TEST(PerformanceStatsCommandProviderTest, ReportsProfileWindowStateAndTextureCosts)
    {
        PerformanceStatsSnapshot expected{};
        expected.profile.summary.complete = true;
        expected.profile.summary.warmup_frames_completed = 120;
        expected.profile.summary.samples_collected = 300;
        expected.profile.summary.cpu_total_p50_ms = 4.5;
        expected.profile.summary.cpu_present_p50_ms = 1.5;
        expected.profile.graph_compile_ms = 0.25;
        expected.profile.textures.dependency_count = 3;
        expected.profile.textures.resident_bytes = 2048;

        command::CommandRegistry registry;
        const auto registration = RegisterPerformanceStatsCommands(
            registry, [expected] { return expected; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        std::optional<command::CommandResult> completed;
        const auto run = [&registry, &completed](const char *text)
        {
            completed.reset();
            const command::CommandResult pending = registry.ExecuteText(
                text, {command::CommandOrigin::Test, command::CommandThread::Immediate},
                [&completed](const command::CommandResult &result) { completed = result; });
            EXPECT_EQ(pending.status, command::CommandStatus::Pending) << pending.message;
            EXPECT_EQ(registry.PumpGameThread(), 1U);
            EXPECT_TRUE(completed.has_value());
        };

        run("gpu-stats --json");
        ASSERT_TRUE(completed.has_value());
        ASSERT_NE(Find(completed->data, "summary_complete"), nullptr);
        EXPECT_TRUE(std::get<bool>(*Find(completed->data, "summary_complete")));
        ASSERT_NE(Find(completed->data, "summary_warmup_frames_completed"), nullptr);
        EXPECT_EQ(std::get<uint64_t>(*Find(completed->data, "summary_warmup_frames_completed")),
                  120U);
        ASSERT_NE(Find(completed->data, "summary_samples_collected"), nullptr);
        EXPECT_EQ(std::get<uint64_t>(*Find(completed->data, "summary_samples_collected")), 300U);
        ASSERT_NE(Find(completed->data, "textures_dependency_count"), nullptr);
        EXPECT_EQ(std::get<uint64_t>(*Find(completed->data, "textures_dependency_count")), 3U);
        ASSERT_NE(Find(completed->data, "textures_resident_bytes"), nullptr);
        EXPECT_EQ(std::get<uint64_t>(*Find(completed->data, "textures_resident_bytes")), 2048U);

        run("cpu-stats --json");
        ASSERT_TRUE(completed.has_value());
        ASSERT_NE(Find(completed->data, "summary_complete"), nullptr);
        EXPECT_TRUE(std::get<bool>(*Find(completed->data, "summary_complete")));
        ASSERT_NE(Find(completed->data, "graph_compile_ms"), nullptr);
        EXPECT_DOUBLE_EQ(std::get<double>(*Find(completed->data, "graph_compile_ms")), 0.25);
        ASSERT_NE(Find(completed->data, "summary_cpu_total_p50_ms"), nullptr);
        EXPECT_DOUBLE_EQ(std::get<double>(*Find(completed->data, "summary_cpu_total_p50_ms")),
                         4.5);
        ASSERT_NE(Find(completed->data, "summary_cpu_present_p50_ms"), nullptr);
        EXPECT_DOUBLE_EQ(std::get<double>(*Find(completed->data, "summary_cpu_present_p50_ms")),
                         1.5);
    }
}
