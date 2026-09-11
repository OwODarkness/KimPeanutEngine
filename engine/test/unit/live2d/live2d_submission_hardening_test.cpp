#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "live2d_render_contract.h"
#include "live2d_render_planner.h"

// L2D4.5 failure-path coverage for the Live2D planner. Every case here must
// publish no generic submission rather than approximate a result.
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
            frame.drawables[0].opacity = 1.0f;
            frame.drawables[0].render_order = 0;
            frame.drawables[1].visible = true;
            frame.drawables[1].opacity = 1.0f;
            frame.drawables[1].render_order = 1;
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

        // Builds a model whose static context count exceeds the frozen V1 atlas
        // capacity, with every consumer visible so no context is dropped.
        Live2DStaticModelData MakeOverCapacityStaticData()
        {
            constexpr std::uint32_t kConsumerCount = kLive2DMaxActiveMaskContexts + 1u;
            Live2DStaticModelData data{};
            data.topology_revision = 88u;
            data.canvas.size_in_pixels = {128.0f, 128.0f};
            data.canvas.pixels_per_unit = 128.0f;
            data.texture_count = 1u;
            // Two drawables per context, three vertices each.
            constexpr std::uint32_t kVertexCount = kConsumerCount * 2u * 3u;
            for (std::uint32_t index = 0u; index < kVertexCount; ++index)
            {
                data.uvs.push_back({static_cast<float>(index % 2u),
                                    static_cast<float>(index % 3u)});
            }
            data.indices.resize(data.uvs.size());
            for (std::size_t index = 0u; index < data.indices.size(); ++index)
            {
                data.indices[index] = static_cast<std::uint16_t>(index % 3u);
            }
            for (std::uint32_t context = 0u; context < kConsumerCount; ++context)
            {
                const std::uint32_t consumer = context * 2u;
                const std::uint32_t source = consumer + 1u;
                data.mask_contexts.push_back({{source}});
                data.drawables.push_back(
                    {consumer * 3u, 3u, consumer * 3u, 3u, 0u, context, {source}});
                data.drawables.push_back(
                    {source * 3u, 3u, source * 3u, 3u, 0u, kLive2DNoMaskContext, {}});
            }
            data.maximum_position_bytes = data.uvs.size() * sizeof(Live2DVector2);
            data.feature_report.drawable_count =
                static_cast<std::uint32_t>(data.drawables.size());
            data.feature_report.active_mask_context_count = kConsumerCount;
            return data;
        }
    }

    TEST(Live2DSubmissionHardeningTest, RejectsSnapshotTopologyRevisionMismatch)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.topology_revision = data.topology_revision + 1u;

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("topology revision"), std::string::npos);
        EXPECT_TRUE(result.submission.work.passes.empty());
        EXPECT_TRUE(result.submission.work.buffer_writes.empty());
    }

    TEST(Live2DSubmissionHardeningTest, RejectsProxyTopologyMismatch)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DStaticModelData stale = data;
        stale.topology_revision = data.topology_revision + 5u;
        const Live2DFrameSnapshot frame = MakeFrame(data);

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(stale), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("does not match the snapshot"),
                  std::string::npos);
        EXPECT_TRUE(result.submission.work.passes.empty());
    }

    TEST(Live2DSubmissionHardeningTest, RejectsSnapshotWithoutFrameSequence)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.frame_sequence = 0u;

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("frame sequence"), std::string::npos);
    }

    TEST(Live2DSubmissionHardeningTest, RejectsStaleDrawableTopology)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.drawables.pop_back();

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("drawable state count"), std::string::npos);
    }

    TEST(Live2DSubmissionHardeningTest, RejectsNonFiniteFrameState)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.positions[0].x = std::numeric_limits<float>::quiet_NaN();

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_TRUE(result.submission.work.passes.empty());
    }

    TEST(Live2DSubmissionHardeningTest, HiddenModelPublishesOnlyAClearedTarget)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.drawables[0].visible = false;
        frame.drawables[1].opacity = 0.0f;

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        // One color pass that clears the target and binds nothing invalid.
        ASSERT_EQ(result.submission.work.passes.size(), 1u);
        EXPECT_TRUE(result.submission.work.passes.front().draws.empty());
        EXPECT_EQ(result.submission.counters.submitted_draw_count, 0u);
        EXPECT_EQ(result.submission.counters.skipped_invisible_count, 2u);
        // The upload is still authored, but nothing binds to it.
        EXPECT_EQ(result.submission.work.buffer_writes.size(), 1u);
    }

    TEST(Live2DSubmissionHardeningTest, AllZeroPositionsStillPublishGeometry)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DFrameSnapshot frame = MakeFrame(data);
        frame.positions.assign(frame.positions.size(), {0.0f, 0.0f});

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        EXPECT_EQ(result.submission.counters.submitted_draw_count, 2u);
        EXPECT_EQ(result.submission.work.passes.size(), 1u);
    }

    TEST(Live2DSubmissionHardeningTest, RejectsInvalidProxyTextureHandle)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DRenderProxy proxy = MakeProxy(data);
        proxy.textures[0] = {};

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), proxy, data, MakeFrame(data));

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("invalid texture handle"), std::string::npos);
        EXPECT_TRUE(result.submission.work.passes.empty());
    }

    TEST(Live2DSubmissionHardeningTest, RejectsProxyTextureCountMismatch)
    {
        const Live2DStaticModelData data = MakeStaticData();
        Live2DRenderProxy proxy = MakeProxy(data);
        proxy.textures.clear();

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), proxy, data, MakeFrame(data));

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("texture count"), std::string::npos);
    }

    TEST(Live2DSubmissionHardeningTest, RejectsMaskedProxyWithoutAtlasResources)
    {
        Live2DStaticModelData data = MakeStaticData();
        data.drawables[0].mask_context_index = 0u;
        data.drawables[0].mask_source_drawable_indices = {1u};
        data.mask_contexts.push_back({{1u}});
        data.feature_report.active_mask_context_count = 1u;
        Live2DRenderProxy proxy = MakeProxy(data);

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), proxy, data, MakeFrame(data));

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("no mask atlas target or texture"),
                  std::string::npos);
        EXPECT_TRUE(result.submission.work.passes.empty());
    }

    TEST(Live2DSubmissionHardeningTest, RejectsMaskContextCapacityOverflow)
    {
        const Live2DStaticModelData data = MakeOverCapacityStaticData();
        Live2DFrameSnapshot frame{};
        frame.topology_revision = data.topology_revision;
        frame.frame_sequence = 1u;
        frame.positions.assign(data.uvs.size(), {0.0f, 0.0f});
        frame.drawables.resize(data.drawables.size());
        for (Live2DDrawableState &state : frame.drawables)
        {
            state.visible = true;
            state.opacity = 1.0f;
        }

        const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
            MakeResources(), MakeProxy(data), data, frame);

        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("mask context count exceeds V1 capacity"),
                  std::string::npos)
            << result.diagnostic;
        EXPECT_TRUE(result.submission.work.passes.empty());
        EXPECT_TRUE(result.submission.work.buffer_writes.empty());
    }

    TEST(Live2DSubmissionHardeningTest, RepeatedSnapshotsEachPublishTheirOwnUpload)
    {
        const Live2DStaticModelData data = MakeStaticData();
        for (std::uint64_t sequence = 1u; sequence <= 8u; ++sequence)
        {
            Live2DFrameSnapshot frame = MakeFrame(data);
            frame.frame_sequence = sequence;
            frame.positions[0].x = static_cast<float>(sequence);

            const Live2DRenderPlanResult result = Live2DRenderPlanner::Plan(
                MakeResources(), MakeProxy(data), data, frame);

            ASSERT_TRUE(result.succeeded) << result.diagnostic;
            EXPECT_EQ(result.submission.frame_sequence, sequence);
            ASSERT_EQ(result.submission.work.buffer_writes.size(), 1u);
            // Each frame carries its own upload, so a later frame can never
            // reuse the previous frame's positions.
            const std::vector<std::byte> &bytes =
                result.submission.work.buffer_writes.front().bytes;
            float first_x = 0.0f;
            std::memcpy(&first_x, bytes.data(), sizeof(float));
            EXPECT_FLOAT_EQ(first_x, static_cast<float>(sequence));
        }
    }
}
