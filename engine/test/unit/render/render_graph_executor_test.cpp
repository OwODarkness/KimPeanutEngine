#include <gtest/gtest.h>

#include <optional>

#include "render/render_graph/render_graph_executor.h"

namespace
{
    using namespace kpengine::render;

    std::optional<CompiledRenderGraph> MakePlan()
    {
        RenderGraphBuilder builder;
        const GraphTextureHandle output = builder.CreateTexture("Output");
        RenderGraphPassDesc description{"Draw", RenderGraphPassCondition::Always, true, true};
        description.user_key = 0;
        const auto pass = builder.AddPass(description);
        const auto written = builder.WriteTexture(pass, output, RenderGraphUsage::ColorAttachment);
        EXPECT_TRUE(written.has_value());
        if (!written.has_value()) return std::nullopt;
        builder.ExportTexture(*written, "Output");
        const RenderGraphCompileResult result = builder.Compile();
        EXPECT_TRUE(result.Succeeded());
        return result.graph;
    }
}

TEST(RenderGraphExecutorTest, DelegatesOrderedExecutionAndFinalizationToCompiledFrame)
{
    const auto plan = MakePlan();
    ASSERT_TRUE(plan.has_value());
    RenderGraphExecutor executor;
    ASSERT_TRUE(executor.BeginFrame(*plan));
    int recorded_passes = 0;
    ASSERT_TRUE(executor.ExecuteRenderer(
        [&recorded_passes](const CompiledRenderGraph::Pass &) {
            ++recorded_passes;
            return true;
        }));
    EXPECT_EQ(recorded_passes, 1);
    EXPECT_FALSE(executor.CanExecuteExternal());
    std::string error;
    EXPECT_TRUE(executor.Finalize(error)) << error;
    EXPECT_EQ(executor.GetOutcome(0), RenderGraphPassOutcome::Executed);
    executor.Abort();
    EXPECT_FALSE(executor.IsActive());
}

TEST(RenderGraphExecutorTest, AbortIsIdempotentAndDoesNotInventSuccessfulOutcomes)
{
    RenderGraphExecutor executor;
    executor.Abort();
    executor.Abort();
    EXPECT_FALSE(executor.IsActive());
    EXPECT_EQ(executor.GetOutcome(1), RenderGraphPassOutcome::NotInPlan);
    std::string error;
    EXPECT_FALSE(executor.Finalize(error));
    EXPECT_FALSE(error.empty());
}
