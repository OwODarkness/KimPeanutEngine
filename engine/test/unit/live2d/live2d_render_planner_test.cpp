#include <gtest/gtest.h>

#include <cstring>

#include "live2d_render_planner.h"

namespace kpengine::live2d
{
    namespace
    {
        Live2DStaticModelData MakeStaticData()
        {
            Live2DStaticModelData data{};
            data.topology_revision = 77u;
            data.canvas.size_in_pixels = {128.0f, 128.0f};
            data.canvas.pixels_per_unit = 128.0f;
            data.texture_count = 1u;
            data.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
                       {1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}};
            data.indices = {0u, 1u, 2u, 0u, 1u, 2u};
            data.drawables = {
                {0u, 3u, 0u, 3u, 0u, kLive2DNoMaskContext, {}},
                {3u, 3u, 3u, 3u, 0u, kLive2DNoMaskContext, {}}};
            data.maximum_position_bytes = data.uvs.size() * sizeof(Live2DVector2);
            data.feature_report.drawable_count = 2u;
            return data;
        }

        Live2DFrameSnapshot MakeFrame(const Live2DStaticModelData &data)
        {
            Live2DFrameSnapshot frame{};
            frame.topology_revision = data.topology_revision;
            frame.frame_sequence = 4u;
            frame.positions = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
                               {1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}};
            frame.drawables.resize(2u);
            frame.drawables[0].visible = true;
            frame.drawables[0].opacity = 0.5f;
            frame.drawables[0].render_order = 2;
            frame.drawables[0].blend_mode = Live2DBlendMode::Additive;
            frame.drawables[0].culling = true;
            frame.drawables[0].multiply_color = {0.5f, 0.75f, 1.0f, 1.0f};
            frame.drawables[1].visible = true;
            frame.drawables[1].opacity = 1.0f;
            frame.drawables[1].render_order = 1;
            frame.drawables[1].blend_mode = Live2DBlendMode::Normal;
            return frame;
        }

        Live2DRenderProxy MakeProxy(const Live2DStaticModelData &data)
        {
            Live2DRenderProxy proxy{};
            proxy.position_buffer = {1u, 0u};
            proxy.uv_buffer = {2u, 0u};
            proxy.index_buffer = {3u, 0u};
            proxy.output_target = {4u, 0u};
            proxy.textures = {{5u, 0u}};
            proxy.static_data = data;
            proxy.output_width = 128u;
            proxy.output_height = 128u;
            return proxy;
        }

        Live2DRenderResourceSet MakeResources()
        {
            Live2DRenderResourceSet resources{};
            resources.normal_culled = {10u, 0u};
            resources.normal_unculled = {11u, 0u};
            resources.additive_culled = {12u, 0u};
            resources.additive_unculled = {13u, 0u};
            resources.multiplicative_culled = {14u, 0u};
            resources.multiplicative_unculled = {15u, 0u};
            resources.sampler = {16u, 0u};
            return resources;
        }
    }

    TEST(Live2DRenderPlannerTest, EmitsStableUnmaskedGenericWork)
    {
        const Live2DStaticModelData data = MakeStaticData();
        const Live2DFrameSnapshot frame = MakeFrame(data);
        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        ASSERT_EQ(result.submission.work.passes.size(), 1u);
        const auto &draws = result.submission.work.passes.front().draws;
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(draws[0].pipeline.id, 11u);
        EXPECT_EQ(draws[0].first_index, 3u);
        EXPECT_EQ(draws[0].vertex_offset, 3);
        EXPECT_EQ(draws[1].pipeline.id, 12u);
        EXPECT_EQ(draws[1].first_index, 0u);
        EXPECT_EQ(draws[1].vertex_offset, 0);
        EXPECT_EQ(result.submission.counters.position_upload_bytes,
                  data.maximum_position_bytes);
        EXPECT_EQ(result.submission.counters.submitted_draw_count, 2u);
        EXPECT_EQ(draws[0].textures.front().texture.id, 5u);

        Live2DDrawConstants constants{};
        ASSERT_EQ(draws[1].uniforms.front().bytes.size(), sizeof(constants));
        std::memcpy(&constants, draws[1].uniforms.front().bytes.data(), sizeof(constants));
        EXPECT_FLOAT_EQ(constants.opacity, 0.5f);
        EXPECT_FLOAT_EQ(constants.multiply_color.r, 0.5f);
    }

    TEST(Live2DRenderPlannerTest, EmitsMaskedDrawablesAsTwoGenericPasses)
    {
        Live2DStaticModelData data = MakeStaticData();
        data.drawables[0].mask_context_index = 0u;
        data.drawables[0].mask_source_drawable_indices = {1u};
        data.mask_contexts.push_back({{1u}});
        data.feature_report.active_mask_context_count = 1u;
        Live2DRenderProxy proxy = MakeProxy(data);
        proxy.mask_atlas_target = {6u, 0u};
        proxy.mask_atlas_texture = {7u, 0u};
        Live2DRenderResourceSet resources = MakeResources();
        resources.mask_culled = {17u, 0u};
        resources.mask_unculled = {18u, 0u};
        resources.masked_normal_culled = {19u, 0u};
        resources.masked_normal_unculled = {20u, 0u};
        resources.masked_additive_culled = {21u, 0u};
        resources.masked_additive_unculled = {22u, 0u};
        resources.masked_multiplicative_culled = {23u, 0u};
        resources.masked_multiplicative_unculled = {24u, 0u};

        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.drawables[0].inverted_mask = true;
        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            resources, proxy, data, frame);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        ASSERT_EQ(result.submission.work.passes.size(), 2u);
        ASSERT_EQ(result.submission.work.passes[0].draws.size(), 1u);
        ASSERT_EQ(result.submission.work.passes[1].draws.size(), 2u);
        EXPECT_EQ(result.submission.work.passes[0].target.id, 6u);
        EXPECT_EQ(result.submission.work.passes[1].target.id, 4u);
        EXPECT_EQ(result.submission.work.passes[0].draws[0].scissor->width, 85u);
        EXPECT_EQ(result.submission.work.passes[1].draws[1].textures.size(), 2u);
        EXPECT_EQ(result.submission.counters.submitted_mask_source_draw_count, 1u);
        Live2DMaskedDrawConstants constants{};
        ASSERT_EQ(result.submission.work.passes[1].draws[1].uniforms.front().bytes.size(),
                  sizeof(constants));
        std::memcpy(&constants,
                    result.submission.work.passes[1].draws[1].uniforms.front().bytes.data(),
                    sizeof(constants));
        EXPECT_EQ(constants.channel, 0u);
        EXPECT_EQ(constants.inverted, 1u);
    }

    TEST(Live2DRenderPlannerTest, RejectsMissingSelectedPipeline)
    {
        const Live2DStaticModelData data = MakeStaticData();
        const Live2DFrameSnapshot frame = MakeFrame(data);
        Live2DRenderResourceSet resources = MakeResources();
        resources.additive_culled = {};
        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            resources, MakeProxy(data), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("selected pipeline"), std::string::npos);
    }
}
