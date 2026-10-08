#include "render/temporal_reprojection.h"
#include "render/submitted_transform_history.h"

#include "gtest/gtest.h"

using namespace kpengine;
using namespace kpengine::render;

TEST(TemporalReprojectionTest, StaticSurfaceHasZeroMotionOnBothClipConventions)
{
    const Vector4f gl_clip{0.2f, -0.4f, 0.0f, 1.0f};
    const Vector4f vk_clip{0.2f, -0.4f, 0.5f, 1.0f};

    const RasterMotionSample gl = ComputeRasterMotionSample(
        gl_clip, gl_clip, {}, {}, 3.0f, true, false);
    const RasterMotionSample vk = ComputeRasterMotionSample(
        vk_clip, vk_clip, {}, {}, 3.0f, true, true);

    EXPECT_TRUE(gl.history_valid);
    EXPECT_TRUE(vk.history_valid);
    EXPECT_FLOAT_EQ(gl.motion_uv.x_, 0.0f);
    EXPECT_FLOAT_EQ(gl.motion_uv.y_, 0.0f);
    EXPECT_FLOAT_EQ(vk.motion_uv.x_, 0.0f);
    EXPECT_FLOAT_EQ(vk.motion_uv.y_, 0.0f);
}

TEST(TemporalReprojectionTest, TranslationReportsPreviousMinusCurrentUv)
{
    const RasterMotionSample sample = ComputeRasterMotionSample(
        {0.4f, 0.0f, 0.0f, 1.0f}, {-0.2f, 0.0f, 0.0f, 1.0f},
        {}, {}, 2.5f, true, false);

    ASSERT_TRUE(sample.history_valid);
    EXPECT_NEAR(sample.motion_uv.x_, -0.3f, 1.0e-6f);
    EXPECT_FLOAT_EQ(sample.motion_uv.y_, 0.0f);
    EXPECT_FLOAT_EQ(sample.view_depth, 2.5f);
}

TEST(TemporalReprojectionTest, ProjectionJitterDoesNotBecomeSurfaceMotion)
{
    const Vector2f current_jitter{0.01f, -0.02f};
    const Vector2f previous_jitter{-0.015f, 0.005f};
    const Vector4f current_clip{0.2f, -0.4f, 0.0f, 1.0f};
    const Vector4f previous_clip{
        0.2f + 2.0f * previous_jitter.x_,
        -0.4f - 2.0f * previous_jitter.y_, 0.0f, 1.0f};
    const Vector4f jittered_current{
        0.2f + 2.0f * current_jitter.x_,
        -0.4f - 2.0f * current_jitter.y_, 0.0f, 1.0f};

    const RasterMotionSample sample = ComputeRasterMotionSample(
        jittered_current, previous_clip, current_jitter, previous_jitter,
        1.0f, true, false);

    ASSERT_TRUE(sample.history_valid);
    EXPECT_NEAR(sample.motion_uv.x_, 0.0f, 1.0e-6f);
    EXPECT_NEAR(sample.motion_uv.y_, 0.0f, 1.0e-6f);
}

TEST(TemporalReprojectionTest, CutsAndInvalidOrOffscreenHistoryReject)
{
    const RasterMotionSample cut = ComputeRasterMotionSample(
        {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
        {}, {}, 1.0f, false, false);
    const RasterMotionSample behind = ComputeRasterMotionSample(
        {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, -1.0f},
        {}, {}, 1.0f, true, false);
    const RasterMotionSample offscreen = ComputeRasterMotionSample(
        {1.2f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
        {}, {}, 1.0f, true, false);

    EXPECT_FALSE(cut.history_valid);
    EXPECT_FALSE(behind.history_valid);
    EXPECT_FALSE(offscreen.history_valid);
}

TEST(TemporalReprojectionTest, DetectsLargeCameraDiscontinuities)
{
    CameraData previous{};
    CameraData current = previous;
    EXPECT_FALSE(IsRasterCameraCut(previous, current));

    current.view[3][0] = 11.0f;
    EXPECT_TRUE(IsRasterCameraCut(previous, current));
    current = previous;
    current.view[0][0] = 0.4f;
    EXPECT_FALSE(IsRasterCameraCut(previous, current));
    current.view[0][0] = 0.6f;
    EXPECT_TRUE(IsRasterCameraCut(previous, current));
    current = previous;
    current.proj[0][0] = 0.06f;
    EXPECT_TRUE(IsRasterCameraCut(previous, current));
}

TEST(SubmittedTransformHistoryTest, AdvancesOnlyAcceptedFramesAndUsesGenerationalIdentity)
{
    SubmittedTransformHistory history;
    const RenderableHandle handle{7, 2};
    MeshProxy proxy{};
    proxy.handle = handle;
    proxy.world_transform.position_.x_ = 1.0f;

    history.BeginFrame({proxy});
    EXPECT_FALSE(history.FindPrevious(handle).has_value());
    history.CommitFrame(true);

    proxy.world_transform.position_.x_ = 2.0f;
    history.BeginFrame({proxy});
    ASSERT_TRUE(history.FindPrevious(handle).has_value());
    EXPECT_FLOAT_EQ(history.FindPrevious(handle)->position_.x_, 1.0f);
    history.CommitFrame(false);

    proxy.world_transform.position_.x_ = 3.0f;
    history.BeginFrame({proxy});
    ASSERT_TRUE(history.FindPrevious(handle).has_value());
    EXPECT_FLOAT_EQ(history.FindPrevious(handle)->position_.x_, 1.0f);
    history.CommitFrame(true);

    const RenderableHandle recycled_handle{7, 3};
    EXPECT_FALSE(history.FindPrevious(recycled_handle).has_value());
}

TEST(SubmittedTransformHistoryTest, RemovedObjectsDoNotKeepHistory)
{
    SubmittedTransformHistory history;
    const RenderableHandle handle{3, 1};
    MeshProxy proxy{};
    proxy.handle = handle;
    history.BeginFrame({proxy});
    history.CommitFrame(true);

    history.BeginFrame({});
    history.CommitFrame(true);
    EXPECT_FALSE(history.FindPrevious(handle).has_value());
}
