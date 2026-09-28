#include <gtest/gtest.h>

#include <array>
#include <optional>

#include "render/render_graph/render_graph_executor.h"
#include "../../support/fake_render_backend.h"

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

TEST(RenderGraphExecutorTest, IgnoredUndeclaredLookupFailsTypedPassRecording)
{
    RenderGraphBuilder builder;
    const GraphBufferHandle declared = builder.ImportBuffer("DeclaredInput");
    RenderGraphPassDesc description{"Draw", RenderGraphPassCondition::Always, true, true};
    description.user_key = 17;
    const auto pass = builder.AddPass(description);
    ASSERT_TRUE(builder.ReadBuffer(pass, declared, RenderGraphUsage::StorageRead));
    const auto plan = builder.Compile();
    ASSERT_TRUE(plan.Succeeded());

    auto probe = std::make_shared<kpengine::test::BackendProbe>();
    kpengine::test::FakeBackend backend(probe);
    RenderGraphBindings bindings;
    const std::array<kpengine::graphics::BufferHandle, 1> physical{{{91, 0}}};
    std::string binding_error;
    ASSERT_TRUE(bindings.AddBuffers(declared, RenderFrameResourceRole::SceneGeometry,
                                    physical, binding_error)) << binding_error;
    RenderGraphExecutor executor;
    ASSERT_TRUE(executor.BeginFrame(*plan.graph, bindings, *backend.GetCommandRecorder()));
    int callback_count = 0;
    ASSERT_TRUE(executor.ExecuteRenderer(
        [](const CompiledRenderGraph::Pass &) { return RenderGraphPassDisposition::Record; },
        [&callback_count](const RenderGraphPassContext &context) {
            ++callback_count;
            (void)context.ResolveTexture(GraphTextureHandle{92, 0});
            return true;
        }));

    const RenderGraphExecutionResult result = executor.Finalize();
    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(executor.GetOutcome(17), RenderGraphPassOutcome::Failed);
    EXPECT_TRUE(result.finalized);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.diagnostic.find("violated graph bindings"), std::string::npos);
}

TEST(RenderGraphExecutorTest, ExternalTransitionFailureStaysFailedAfterDuplicateRejection)
{
    RenderGraphBuilder builder;
    const GraphTextureHandle imported = builder.ImportTexture("ImportedOutput");
    RenderGraphPassDesc description{"Present", RenderGraphPassCondition::Optional,
                                    true, true, RenderGraphPassOwner::External, true};
    description.user_key = 29;
    const auto pass = builder.AddPass(description);
    builder.ReadTexture(pass, imported, RenderGraphUsage::Sampled);
    const auto plan = builder.Compile();
    ASSERT_TRUE(plan.Succeeded());

    auto probe = std::make_shared<kpengine::test::BackendProbe>();
    kpengine::test::FakeBackend backend(probe);
    RenderGraphBindings bindings;
    RenderGraphExecutor executor;
    ASSERT_TRUE(executor.BeginFrame(*plan.graph, bindings, *backend.GetCommandRecorder()));
    ASSERT_TRUE(executor.ExecuteRenderer(
        [](const CompiledRenderGraph::Pass &) { return true; }));

    int callback_count = 0;
    EXPECT_FALSE(executor.ExecuteExternal([&callback_count] { ++callback_count; }, {}, {}));
    EXPECT_FALSE(executor.ExecuteExternal([&callback_count] { ++callback_count; }, {}, {}));
    const RenderGraphExecutionResult result = executor.Finalize();

    EXPECT_EQ(callback_count, 0);
    EXPECT_EQ(executor.GetOutcome(29), RenderGraphPassOutcome::Failed);
    EXPECT_TRUE(result.finalized);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.diagnostic.find("External terminal transition failed"), std::string::npos);
}
