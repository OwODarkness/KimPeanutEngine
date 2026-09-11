#include <gtest/gtest.h>

#include <utility>

#include "live2d_mask_planner.h"

namespace kpengine::live2d
{
    namespace
    {
        void AddTriangle(Live2DStaticModelData &data,
                         const std::uint32_t texture_index,
                         const std::uint32_t mask_context,
                         std::vector<std::uint32_t> sources)
        {
            const std::uint32_t vertex_offset =
                static_cast<std::uint32_t>(data.uvs.size());
            const std::uint32_t first_index =
                static_cast<std::uint32_t>(data.indices.size());
            data.uvs.insert(data.uvs.end(),
                            {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}});
            data.indices.insert(data.indices.end(), {0u, 1u, 2u});
            data.drawables.push_back({vertex_offset, 3u, first_index, 3u,
                                      texture_index, mask_context,
                                      std::move(sources)});
        }

        Live2DStaticModelData MakeSharedContextData()
        {
            Live2DStaticModelData data{};
            data.topology_revision = 9u;
            data.canvas.size_in_pixels = {128.0f, 128.0f};
            data.canvas.pixels_per_unit = 128.0f;
            data.texture_count = 1u;
            AddTriangle(data, 0u, kLive2DNoMaskContext, {});
            AddTriangle(data, 0u, 0u, {0u});
            AddTriangle(data, 0u, kLive2DNoMaskContext, {});
            AddTriangle(data, 0u, 0u, {0u});
            AddTriangle(data, 0u, 1u, {2u});
            data.mask_contexts = {{{0u}}, {{2u}}};
            data.maximum_position_bytes =
                data.uvs.size() * sizeof(Live2DVector2);
            data.feature_report.drawable_count =
                static_cast<std::uint32_t>(data.drawables.size());
            data.feature_report.active_mask_context_count =
                static_cast<std::uint32_t>(data.mask_contexts.size());
            return data;
        }

        Live2DFrameSnapshot MakeSharedContextFrame(
            const Live2DStaticModelData &data)
        {
            Live2DFrameSnapshot frame{};
            frame.topology_revision = data.topology_revision;
            frame.frame_sequence = 1u;
            frame.positions = {
                {0.0f, 0.0f},  {2.0f, 0.0f},  {0.0f, 2.0f},
                {10.0f, 10.0f}, {20.0f, 10.0f}, {10.0f, 20.0f},
                {30.0f, 30.0f}, {32.0f, 30.0f}, {30.0f, 32.0f},
                {11.0f, 11.0f}, {21.0f, 11.0f}, {11.0f, 21.0f},
                {30.0f, 30.0f}, {40.0f, 30.0f}, {30.0f, 40.0f}};
            frame.drawables.resize(data.drawables.size());
            for (Live2DDrawableState &state : frame.drawables)
            {
                state.visible = true;
                state.opacity = 1.0f;
            }
            return frame;
        }
    }

    TEST(Live2DMaskAtlasPlannerTest, SharesEqualSourcesAndAssignsStableSlots)
    {
        const Live2DStaticModelData data = MakeSharedContextData();
        const Live2DFrameSnapshot frame = MakeSharedContextFrame(data);
        const Live2DMaskAtlasPlanResult result =
            Live2DMaskAtlasPlanner::Plan(data, frame);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        ASSERT_EQ(result.plan.contexts.size(), 2u);
        EXPECT_EQ(result.plan.contexts[0].source_context_index, 0u);
        EXPECT_EQ(result.plan.contexts[0].consumer_drawable_indices,
                  (std::vector<std::uint32_t>{1u, 3u}));
        EXPECT_EQ(result.plan.contexts[0].region.channel, 0u);
        EXPECT_EQ(result.plan.contexts[0].region.region, 0u);
        EXPECT_EQ(result.plan.contexts[1].region.channel, 0u);
        EXPECT_EQ(result.plan.contexts[1].region.region, 1u);
        EXPECT_EQ(result.plan.drawable_context_indices[1], 0u);
        EXPECT_EQ(result.plan.drawable_context_indices[3], 0u);
        EXPECT_EQ(result.plan.drawable_context_indices[4], 1u);
        EXPECT_NE(result.plan.contexts[0].region.model_to_mask[0], 0.0f);
        EXPECT_NE(result.plan.contexts[0].region.model_to_atlas_sample[0], 0.0f);
    }

    TEST(Live2DMaskAtlasPlannerTest, IgnoresContextsWithoutVisibleConsumers)
    {
        const Live2DStaticModelData data = MakeSharedContextData();
        Live2DFrameSnapshot frame = MakeSharedContextFrame(data);
        frame.drawables[1].visible = false;
        frame.drawables[3].visible = false;

        const Live2DMaskAtlasPlanResult result =
            Live2DMaskAtlasPlanner::Plan(data, frame);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        ASSERT_EQ(result.plan.contexts.size(), 1u);
        EXPECT_EQ(result.plan.contexts[0].source_context_index, 1u);
        EXPECT_EQ(result.plan.drawable_context_indices[1], kLive2DNoMaskContext);
        EXPECT_EQ(result.plan.drawable_context_indices[4], 0u);
    }

    TEST(Live2DMaskAtlasPlannerTest, RejectsZeroAreaConsumerBounds)
    {
        const Live2DStaticModelData data = MakeSharedContextData();
        Live2DFrameSnapshot frame = MakeSharedContextFrame(data);
        frame.drawables[3].visible = false;
        frame.positions[3] = {10.0f, 10.0f};
        frame.positions[4] = {10.0f, 10.0f};
        frame.positions[5] = {10.0f, 10.0f};

        const Live2DMaskAtlasPlanResult result =
            Live2DMaskAtlasPlanner::Plan(data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("zero-area"), std::string::npos);
    }

    TEST(Live2DMaskAtlasPlannerTest, RejectsCapacityOverflow)
    {
        Live2DStaticModelData data{};
        data.topology_revision = 10u;
        data.canvas.size_in_pixels = {256.0f, 256.0f};
        data.canvas.pixels_per_unit = 256.0f;
        data.texture_count = 1u;
        for (std::uint32_t context = 0u;
             context <= kLive2DMaxActiveMaskContexts; ++context)
        {
            const std::uint32_t source =
                static_cast<std::uint32_t>(data.drawables.size());
            AddTriangle(data, 0u, kLive2DNoMaskContext, {});
            const std::uint32_t consumer =
                static_cast<std::uint32_t>(data.drawables.size());
            AddTriangle(data, 0u, context, {source});
            data.mask_contexts.push_back({{source}});
            (void)consumer;
        }
        data.maximum_position_bytes =
            data.uvs.size() * sizeof(Live2DVector2);
        data.feature_report.drawable_count =
            static_cast<std::uint32_t>(data.drawables.size());
        data.feature_report.active_mask_context_count =
            static_cast<std::uint32_t>(data.mask_contexts.size());

        Live2DFrameSnapshot frame{};
        frame.topology_revision = data.topology_revision;
        frame.frame_sequence = 1u;
        frame.positions.resize(data.uvs.size());
        frame.drawables.resize(data.drawables.size());
        for (std::size_t index = 0u; index < frame.drawables.size(); ++index)
        {
            frame.positions[index * 2u] = {static_cast<float>(index), 0.0f};
            frame.positions[index * 2u + 1u] =
                {static_cast<float>(index), 1.0f};
            frame.drawables[index].visible = true;
            frame.drawables[index].opacity = 1.0f;
        }

        const Live2DMaskAtlasPlanResult result =
            Live2DMaskAtlasPlanner::Plan(data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("capacity"), std::string::npos);
    }
}
