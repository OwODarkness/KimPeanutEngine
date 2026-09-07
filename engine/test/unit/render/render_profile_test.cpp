#include <gtest/gtest.h>

#include "render/render_profile.h"

TEST(RenderProfileScenario, SponzaBaselineIsStable)
{
    const kpengine::render::RenderProfileScenario scenario =
        kpengine::render::GetSponzaProfileScenario();

    EXPECT_STREQ(scenario.name, "sponza-stage-0");
    EXPECT_STREQ(scenario.startup_level, "level/sponza.level");
    EXPECT_STREQ(scenario.camera_id, "main_camera");
    EXPECT_STREQ(scenario.build_type, "Debug");
    EXPECT_EQ(scenario.graphics_api, kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN);
    EXPECT_EQ(scenario.viewport_width, 1920U);
    EXPECT_EQ(scenario.viewport_height, 1080U);
    EXPECT_EQ(scenario.warmup_frames, 120U);
    EXPECT_EQ(scenario.sample_frames, 300U);
}

TEST(RenderProfileSnapshot, StartsWithNoMeasurements)
{
    const kpengine::render::RenderProfileSnapshot snapshot{};

    EXPECT_EQ(snapshot.draw_calls, 0U);
    EXPECT_EQ(snapshot.sections, 0U);
    EXPECT_EQ(snapshot.descriptor_sets_created, 0U);
    EXPECT_EQ(snapshot.descriptor_pools_created, 0U);
    EXPECT_EQ(snapshot.textures.dependency_count, 0U);
    EXPECT_EQ(snapshot.passes.size(),
              static_cast<size_t>(kpengine::render::RenderProfilePass::Count));
    for (const auto &pass : snapshot.passes)
    {
        EXPECT_DOUBLE_EQ(pass.cpu_time_ms, 0.0);
        EXPECT_FALSE(pass.gpu_time_ms.has_value());
    }
}

TEST(RenderProfileWindow, IgnoresWarmupAndComputesPercentiles)
{
    kpengine::render::RenderProfileWindow window(2, 3);
    kpengine::render::RenderProfileSnapshot snapshot{};

    snapshot.cpu_total_ms = 1.0;
    window.Observe(snapshot);
    window.Observe(snapshot);
    snapshot.cpu_total_ms = 10.0;
    window.Observe(snapshot);
    snapshot.cpu_total_ms = 20.0;
    window.Observe(snapshot);
    snapshot.cpu_total_ms = 30.0;
    window.Observe(snapshot);

    const auto summary = window.GetSummary();
    EXPECT_TRUE(summary.complete);
    EXPECT_EQ(summary.warmup_frames_completed, 2U);
    EXPECT_EQ(summary.samples_collected, 3U);
    EXPECT_DOUBLE_EQ(summary.cpu_total_p50_ms, 20.0);
    EXPECT_DOUBLE_EQ(summary.cpu_total_p95_ms, 29.0);
}
