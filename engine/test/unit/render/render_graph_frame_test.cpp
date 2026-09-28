#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "render/render_graph/render_graph.h"
#include "render/render_graph/render_graph_frame.h"
#include "render/path_trace_settings.h"

namespace
{
    using kpengine::render::GraphTextureHandle;
    using kpengine::render::RenderGraphBuilder;
    using kpengine::render::RenderGraphCompileResult;
    using kpengine::render::RenderGraphFrame;
    using kpengine::render::RenderGraphPassCondition;
    using kpengine::render::RenderGraphPassFailurePolicy;
    using kpengine::render::RenderGraphPassOutcome;
    using kpengine::render::RenderGraphPassOwner;

    // Shaped like the raster declaration: required renderer passes feeding an
    // optional conversion pass, then an external composition terminal.
    constexpr uint64_t kShadowKey = 1;
    constexpr uint64_t kLightingKey = 2;
    constexpr uint64_t kCaptureKey = 3;
    constexpr uint64_t kCompositeKey = 4;

    RenderGraphCompileResult CompileFrameGraph(bool capture_enabled)
    {
        RenderGraphBuilder builder;
        const GraphTextureHandle shadow_map = builder.CreateTexture("DirectionalShadow");
        const GraphTextureHandle scene_color = builder.CreateTexture("SceneColor");
        const GraphTextureHandle capture_output = builder.CreateTexture("CaptureOutput");

        const auto shadow = builder.AddPass({"Shadow", RenderGraphPassCondition::Always, true,
                                             false, RenderGraphPassOwner::Renderer, false,
                                             kShadowKey});
        const auto shadow_version = builder.WriteTexture(shadow, shadow_map);
        if (!shadow_version.has_value())
        {
            return builder.Compile();
        }

        const auto lighting = builder.AddPass({"Lighting", RenderGraphPassCondition::Always, true,
                                               false, RenderGraphPassOwner::Renderer, false,
                                               kLightingKey});
        builder.ReadTexture(lighting, *shadow_version);
        const auto color_version = builder.WriteTexture(lighting, scene_color);
        if (!color_version.has_value())
        {
            return builder.Compile();
        }

        const auto capture = builder.AddPass({"Capture", RenderGraphPassCondition::Optional,
                                              capture_enabled, false,
                                              RenderGraphPassOwner::Renderer, false, kCaptureKey,
                                              RenderGraphPassFailurePolicy::Optional});
        builder.ReadTexture(capture, *color_version);
        const auto capture_version = builder.WriteTexture(capture, capture_output);
        if (!capture_version.has_value())
        {
            return builder.Compile();
        }
        if (capture_enabled)
        {
            builder.ExportTexture(*capture_version, "CaptureOutput");
        }

        const auto composite = builder.AddPass({"Composite", RenderGraphPassCondition::Optional,
                                                true, true, RenderGraphPassOwner::External, true,
                                                kCompositeKey});
        builder.ReadTexture(composite, *color_version);
        return builder.Compile();
    }
}

TEST(RenderGraphFrameTest, VisitsRendererPassesInCompiledOrderAndRecordsOutcomes)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);

    std::vector<uint64_t> visited;
    ASSERT_TRUE(frame.ExecuteRenderer([&visited](const auto &pass) {
        visited.push_back(*pass.user_key);
        return true;
    }));
    ASSERT_EQ(visited.size(), 3U);
    EXPECT_EQ(visited[0], kShadowKey);
    EXPECT_EQ(visited[1], kLightingKey);
    EXPECT_EQ(visited[2], kCaptureKey);

    EXPECT_EQ(frame.GetOutcome(kShadowKey), RenderGraphPassOutcome::Executed);
    EXPECT_EQ(frame.GetOutcome(kLightingKey), RenderGraphPassOutcome::Executed);
    EXPECT_EQ(frame.GetOutcome(kCaptureKey), RenderGraphPassOutcome::Executed);
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::Pending);
    EXPECT_FALSE(frame.HasRequiredFailure());
}

TEST(RenderGraphFrameTest, FinalizeSkipsTheUnrequestedExternalTerminal)
{
    const auto result = CompileFrameGraph(false);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);
    ASSERT_TRUE(frame.ExecuteRenderer([](const auto &) { return true; }));

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
    EXPECT_TRUE(frame.IsFinalized());
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::SkippedExternal);
    EXPECT_FALSE(frame.ExecuteRenderer([](const auto &) { return true; }));
    EXPECT_FALSE(frame.ExecuteExternal([] { return true; }));
}

TEST(RenderGraphFrameTest, ExternalTerminalIsExactlyOnceAndCannotRunPrematurely)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);

    EXPECT_FALSE(frame.ExecuteExternal([] { return true; }));

    std::size_t executions = 0;
    ASSERT_TRUE(frame.ExecuteRenderer([](const auto &) { return true; }));
    ASSERT_TRUE(frame.ExecuteExternal([&executions] { ++executions; return true; }));
    EXPECT_FALSE(frame.ExecuteExternal([&executions] { ++executions; return true; }));

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
    EXPECT_EQ(executions, 1U);
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::Executed);
}

TEST(RenderGraphFrameTest, SkipsDependentOutputsAfterRequiredFailure)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);

    std::vector<uint64_t> visited;
    ASSERT_TRUE(frame.ExecuteRenderer([&visited](const auto &pass) {
        visited.push_back(*pass.user_key);
        return *pass.user_key != kLightingKey;
    }));
    ASSERT_EQ(visited.size(), 2U);
    EXPECT_EQ(frame.GetOutcome(kLightingKey), RenderGraphPassOutcome::Failed);
    EXPECT_EQ(frame.GetOutcome(kCaptureKey), RenderGraphPassOutcome::SkippedDependency);
    EXPECT_TRUE(frame.HasRequiredFailure());
    EXPECT_FALSE(frame.CanExecuteExternal());
    EXPECT_TRUE(frame.ExecuteExternal([] { return true; }));
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::SkippedDependency);

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
}

TEST(RenderGraphFrameTest, OptionalCaptureFailureDoesNotPoisonSceneOutput)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);

    ASSERT_TRUE(frame.ExecuteRenderer([](const auto &pass) {
        return !pass.user_key.has_value() || *pass.user_key != kCaptureKey;
    }));
    EXPECT_EQ(frame.GetOutcome(kCaptureKey), RenderGraphPassOutcome::Failed);
    EXPECT_FALSE(frame.HasRequiredFailure());
    EXPECT_TRUE(frame.CanExecuteExternal());
    ASSERT_TRUE(frame.ExecuteExternal([] { return true; }));
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::Executed);

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
}

TEST(RenderGraphFrameTest, ExternalRecordingFailureIsRetainedAsFailed)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);
    ASSERT_TRUE(frame.ExecuteRenderer([](const auto &) { return true; }));

    EXPECT_FALSE(frame.ExecuteExternal([] { return false; }));
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::Failed);
    EXPECT_TRUE(frame.HasRequiredFailure());
    EXPECT_FALSE(frame.ExecuteExternal([] { return true; }));

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::Failed);
    EXPECT_TRUE(frame.HasRequiredFailure());
}

TEST(RenderGraphFrameTest, CulledOptionalPassIsNotInPlan)
{
    const auto result = CompileFrameGraph(false);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);

    EXPECT_EQ(frame.GetOutcome(kCaptureKey), RenderGraphPassOutcome::NotInPlan);
    EXPECT_EQ(frame.GetOutcome(kCompositeKey), RenderGraphPassOutcome::Pending);

    std::vector<uint64_t> visited;
    ASSERT_TRUE(frame.ExecuteRenderer([&visited](const auto &pass) {
        visited.push_back(*pass.user_key);
        return true;
    }));
    ASSERT_EQ(visited.size(), 2U);

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
    EXPECT_EQ(frame.GetOutcome(kCaptureKey), RenderGraphPassOutcome::NotInPlan);
    EXPECT_EQ(frame.GetOutcome(999ULL), RenderGraphPassOutcome::NotInPlan);
}

TEST(RenderGraphFrameTest, MovingFrameInvalidatesTheMovedFromFrame)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);
    RenderGraphFrame moved(std::move(frame));

    ASSERT_TRUE(moved.ExecuteRenderer([](const auto &) { return true; }));
    EXPECT_FALSE(frame.ExecuteRenderer([](const auto &) { return true; }));
    EXPECT_FALSE(frame.ExecuteExternal([] { return true; }));
    std::string error;
    EXPECT_FALSE(frame.Finalize(error));
    EXPECT_EQ(frame.GetOutcome(kShadowKey), RenderGraphPassOutcome::NotInPlan);
}

TEST(RenderGraphFrameTest, RejectsFinalizeBeforeRendererExecution)
{
    const auto result = CompileFrameGraph(true);
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);

    std::string error;
    EXPECT_FALSE(frame.Finalize(error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(frame.IsFinalized());
}

TEST(RenderGraphFrameTest, RejectsAPassAfterTheExternalTerminal)
{
    RenderGraphBuilder builder;
    const GraphTextureHandle shadow_map = builder.CreateTexture("DirectionalShadow");
    builder.AddPass({"Composite", RenderGraphPassCondition::Optional, true, true,
                     RenderGraphPassOwner::External, true, kCompositeKey});
    const auto shadow = builder.AddPass({"Shadow", RenderGraphPassCondition::Always, true, false,
                                         RenderGraphPassOwner::Renderer, false, kShadowKey});
    const auto shadow_version = builder.WriteTexture(shadow, shadow_map);
    ASSERT_TRUE(shadow_version.has_value());
    builder.ExportTexture(*shadow_version, "DirectionalShadow");

    const auto result = builder.Compile();
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.graph->Passes().size(), 2U);
    ASSERT_EQ(result.graph->Passes()[0].owner, RenderGraphPassOwner::External);

    RenderGraphFrame frame(*result.graph);
    ASSERT_TRUE(frame.ExecuteRenderer([](const auto &) { return true; }));
    std::string error;
    EXPECT_FALSE(frame.Finalize(error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(frame.GetOutcome(kShadowKey), RenderGraphPassOutcome::Pending);
}

TEST(PathTraceSettingsTest, ValidatesIndependentSettingsAndRejectsEnumHoles)
{
    using kpengine::render::IsValidPathTraceSettings;
    using kpengine::render::PathTraceOutputProbe;
    using kpengine::render::PathTraceSettings;

    PathTraceSettings settings;
    settings.visibility_method = kpengine::render::PathTraceVisibilityMethod::RayQuery;
    settings.reconstruction = kpengine::render::PathTraceReconstruction::VarianceDenoise;
    settings.samples_per_dispatch = 2;
    EXPECT_TRUE(IsValidPathTraceSettings(settings));

    settings.output_probe = static_cast<PathTraceOutputProbe>(6);
    EXPECT_FALSE(IsValidPathTraceSettings(settings));
}

TEST(PathTraceSettingsTest, LegacyAdapterPreservesUnrelatedPolicy)
{
    using kpengine::render::ApplyLegacyPathTraceProbeMode;
    using kpengine::render::PathTraceProbeMode;
    using kpengine::render::PathTraceSettings;

    PathTraceSettings settings;
    settings.path_tracing_enabled = false;
    settings.hybrid_ray_query_shadows_enabled = false;
    settings.maximum_continuation_bounces = 12;
    const PathTraceSettings adapted =
        ApplyLegacyPathTraceProbeMode(settings, PathTraceProbeMode::PrimaryNormal);

    EXPECT_FALSE(adapted.path_tracing_enabled);
    EXPECT_FALSE(adapted.hybrid_ray_query_shadows_enabled);
    EXPECT_EQ(adapted.maximum_continuation_bounces, 12U);
    EXPECT_EQ(adapted.output_probe, kpengine::render::PathTraceOutputProbe::PrimaryNormal);
}

TEST(RenderGraphFrameTest, FinalizesAGraphWithoutAnExternalTerminal)
{
    RenderGraphBuilder builder;
    const GraphTextureHandle texture = builder.CreateTexture("Color");
    const auto pass = builder.AddPass({"Render", RenderGraphPassCondition::Always, true, false,
                                       RenderGraphPassOwner::Renderer, false, kShadowKey});
    const auto version = builder.WriteTexture(pass, texture);
    ASSERT_TRUE(version.has_value());
    builder.ExportTexture(*version, "Color");

    const auto result = builder.Compile();
    ASSERT_TRUE(result.Succeeded());
    RenderGraphFrame frame(*result.graph);
    ASSERT_TRUE(frame.ExecuteRenderer([](const auto &) { return true; }));

    std::string error;
    EXPECT_TRUE(frame.Finalize(error)) << error;
    EXPECT_EQ(frame.GetOutcome(kShadowKey), RenderGraphPassOutcome::Executed);
}
