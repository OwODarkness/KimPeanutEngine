#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "graphics/backend/common/buffer_types.h"
#include "panel_render_planner.h"
#include "render/render_submission.h"

namespace kpengine::panel
{
    namespace
    {
        // A proxy whose every handle is valid, so a test can invalidate exactly
        // one field and attribute the failure to it.
        PanelRenderProxy MakeValidProxy()
        {
            PanelRenderProxy proxy;
            proxy.pipeline = graphics::PipelineHandle{1u, 1u};
            proxy.dot_mask = graphics::TextureHandle{2u, 1u};
            proxy.sampler = graphics::SamplerHandle{3u, 1u};
            proxy.quad_vertices = graphics::BufferHandle{4u, 1u};
            proxy.quad_indices = graphics::BufferHandle{5u, 1u};
            proxy.output_target = graphics::RenderTargetHandle{6u, 1u};
            proxy.output_width = 64u;
            proxy.output_height = 32u;
            proxy.index_count = kPanelQuadIndexCount;
            return proxy;
        }
    }

    TEST(PanelRenderPlannerTest, EmitsOneTargetPassWithOneQuadDraw)
    {
        const PanelRenderPlanResult plan =
            PanelRenderPlanner::Plan(MakeValidProxy());

        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;
        ASSERT_EQ(plan.work.passes.size(), 1u);

        const render::SubmissionPass &pass = plan.work.passes.front();
        // A pass is a target XOR a presentation pass, never both.
        EXPECT_FALSE(pass.presentation);
        EXPECT_TRUE(pass.target.IsValid());
        ASSERT_EQ(pass.draws.size(), 1u);

        const render::SubmissionDraw &draw = pass.draws.front();
        ASSERT_EQ(draw.geometry.vertices.size(), 1u);
        EXPECT_EQ(draw.geometry.vertices.front().binding, 0u);
        EXPECT_TRUE(draw.geometry.indices.buffer.IsValid());
        EXPECT_EQ(draw.geometry.indices.type, graphics::IndexElementType::UInt16);
        EXPECT_EQ(draw.index_count, kPanelQuadIndexCount);
    }

    TEST(PanelRenderPlannerTest, DrawsThePanelGeometryWithThePanelPipeline)
    {
        const PanelRenderProxy proxy = MakeValidProxy();
        const PanelRenderPlanResult plan = PanelRenderPlanner::Plan(proxy);

        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;
        const render::SubmissionDraw &draw = plan.work.passes.front().draws.front();
        EXPECT_EQ(draw.pipeline.id, proxy.pipeline.id);
        EXPECT_EQ(draw.geometry.vertices.front().buffer.id, proxy.quad_vertices.id);
        EXPECT_EQ(draw.geometry.indices.buffer.id, proxy.quad_indices.id);
    }

    TEST(PanelRenderPlannerTest, BindsTheConstantsAndTheDotMaskAtSetZero)
    {
        const PanelRenderPlanResult plan =
            PanelRenderPlanner::Plan(MakeValidProxy());

        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;
        const render::SubmissionDraw &draw = plan.work.passes.front().draws.front();

        // Two bindings, and neither may repeat: the submission contract rejects
        // a duplicate (set, binding) pair.
        ASSERT_EQ(draw.uniforms.size(), 1u);
        ASSERT_EQ(draw.textures.size(), 1u);

        EXPECT_EQ(draw.uniforms.front().set, 0u);
        EXPECT_EQ(draw.uniforms.front().binding, kPanelConstantsBinding);
        EXPECT_EQ(draw.uniforms.front().bytes.size(), sizeof(PanelDrawConstants));

        EXPECT_EQ(draw.textures.front().set, 0u);
        EXPECT_EQ(draw.textures.front().binding, kPanelDotMaskBinding);
        EXPECT_TRUE(draw.textures.front().sampler.IsValid());
    }

    TEST(PanelRenderPlannerTest, CarriesTheRequestedColorsInTheConstantBlock)
    {
        PanelRenderPlanOptions options;
        options.dot_color = {0.25f, 0.5f, 0.75f, 1.0f};
        options.background_color = {0.0f, 0.0f, 0.0f, 1.0f};

        const PanelRenderPlanResult plan =
            PanelRenderPlanner::Plan(MakeValidProxy(), options);
        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;

        const std::vector<std::byte> &bytes =
            plan.work.passes.front().draws.front().uniforms.front().bytes;
        ASSERT_EQ(bytes.size(), sizeof(PanelDrawConstants));

        PanelDrawConstants read_back{};
        std::memcpy(&read_back, bytes.data(), sizeof(read_back));
        EXPECT_FLOAT_EQ(read_back.dot_color[0], 0.25f);
        EXPECT_FLOAT_EQ(read_back.dot_color[2], 0.75f);
        EXPECT_FLOAT_EQ(read_back.background_color[0], 0.0f);
    }

    TEST(PanelRenderPlannerTest, CarriesTheDotGapAndLeavesTheReservedLanesZero)
    {
        PanelRenderPlanOptions options;
        options.dot_gap = 0.3f;

        const PanelRenderPlanResult plan =
            PanelRenderPlanner::Plan(MakeValidProxy(), options);
        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;

        const std::vector<std::byte> &bytes =
            plan.work.passes.front().draws.front().uniforms.front().bytes;
        ASSERT_EQ(bytes.size(), sizeof(PanelDrawConstants));

        PanelDrawConstants read_back{};
        std::memcpy(&read_back, bytes.data(), sizeof(read_back));
        EXPECT_FLOAT_EQ(read_back.params[0], 0.3f);
        // The reserved lanes must be written rather than left indeterminate: the
        // shader reads the whole vec4, and the producer's struct may be built on
        // a stack frame that held anything.
        EXPECT_FLOAT_EQ(read_back.params[1], 0.0f);
        EXPECT_FLOAT_EQ(read_back.params[2], 0.0f);
        EXPECT_FLOAT_EQ(read_back.params[3], 0.0f);
    }

    TEST(PanelRenderPlannerTest, DefaultsToAVisibleDotGap)
    {
        // Without a gap, adjacent lit dots merge into solid runs and the panel
        // stops reading as a matrix of elements.
        const PanelRenderPlanResult plan =
            PanelRenderPlanner::Plan(MakeValidProxy());
        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;

        const std::vector<std::byte> &bytes =
            plan.work.passes.front().draws.front().uniforms.front().bytes;
        PanelDrawConstants read_back{};
        std::memcpy(&read_back, bytes.data(), sizeof(read_back));

        EXPECT_FLOAT_EQ(read_back.params[0], kPanelDefaultDotGap);
        EXPECT_GT(kPanelDefaultDotGap, 0.0f);
        EXPECT_LT(kPanelDefaultDotGap, 0.5f);
    }

    TEST(PanelRenderPlannerTest, CoversTheWholeOutputWithTheViewport)
    {
        const PanelRenderPlanResult plan =
            PanelRenderPlanner::Plan(MakeValidProxy());

        ASSERT_TRUE(plan.succeeded) << plan.diagnostic;
        const render::SubmissionDraw &draw = plan.work.passes.front().draws.front();
        EXPECT_FLOAT_EQ(draw.viewport.width, 64.0f);
        EXPECT_FLOAT_EQ(draw.viewport.height, 32.0f);
        EXPECT_LE(draw.viewport.min_depth, draw.viewport.max_depth);
        // No scissor: scissor state is sticky within a target, so a pass that
        // never sets one must not appear to.
        EXPECT_FALSE(draw.scissor.has_value());
    }

    TEST(PanelRenderPlannerTest, PublishesNothingWhenAnInputIsMissing)
    {
        const auto expect_rejected = [](const PanelRenderProxy &proxy)
        {
            const PanelRenderPlanResult plan = PanelRenderPlanner::Plan(proxy);
            EXPECT_FALSE(plan.succeeded);
            // A failed plan must carry no work at all, so a caller cannot
            // record half a panel.
            EXPECT_TRUE(plan.work.passes.empty());
            EXPECT_TRUE(plan.work.buffer_writes.empty());
            EXPECT_FALSE(plan.diagnostic.empty());
        };

        PanelRenderProxy no_pipeline = MakeValidProxy();
        no_pipeline.pipeline = {};
        expect_rejected(no_pipeline);

        PanelRenderProxy no_mask = MakeValidProxy();
        no_mask.dot_mask = {};
        expect_rejected(no_mask);

        PanelRenderProxy no_sampler = MakeValidProxy();
        no_sampler.sampler = {};
        expect_rejected(no_sampler);

        PanelRenderProxy no_target = MakeValidProxy();
        no_target.output_target = {};
        expect_rejected(no_target);

        PanelRenderProxy no_vertices = MakeValidProxy();
        no_vertices.quad_vertices = {};
        expect_rejected(no_vertices);

        PanelRenderProxy no_indices = MakeValidProxy();
        no_indices.quad_indices = {};
        expect_rejected(no_indices);

        PanelRenderProxy no_indices_to_draw = MakeValidProxy();
        no_indices_to_draw.index_count = 0u;
        expect_rejected(no_indices_to_draw);

        PanelRenderProxy no_extent = MakeValidProxy();
        no_extent.output_width = 0u;
        expect_rejected(no_extent);
    }

    TEST(PanelRenderPlannerTest, IsDeterministicForTheSameProxy)
    {
        const PanelRenderProxy proxy = MakeValidProxy();
        const PanelRenderPlanResult first = PanelRenderPlanner::Plan(proxy);
        const PanelRenderPlanResult second = PanelRenderPlanner::Plan(proxy);

        ASSERT_TRUE(first.succeeded);
        ASSERT_TRUE(second.succeeded);
        ASSERT_EQ(first.work.passes.size(), second.work.passes.size());
        EXPECT_EQ(first.work.passes.front().draws.front().uniforms.front().bytes,
                  second.work.passes.front().draws.front().uniforms.front().bytes);
    }
}
