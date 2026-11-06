#include <gtest/gtest.h>

#include "render/camera_utils.h"
#include "render/render_camera.h"
#include <limits>

namespace
{
    using kpengine::Matrix4f;
    using kpengine::render::camera::IsAABBInsidePerspectiveFace;
    using kpengine::spatial::AABB;
}

TEST(CameraUtilsTest, AcceptsBoundsWithACornerInsidePerspectiveFace)
{
    const Matrix4f view = Matrix4f::Identity();

    EXPECT_TRUE(IsAABBInsidePerspectiveFace(
        AABB{{-0.5f, -0.5f, -2.0f}, {0.5f, 0.5f, -1.0f}}, view, 0.1f, 10.0f));
}

TEST(CameraUtilsTest, RejectsBoundsOutsidePerspectiveFace)
{
    const Matrix4f view = Matrix4f::Identity();

    EXPECT_FALSE(IsAABBInsidePerspectiveFace(
        AABB{{3.0f, -0.5f, -2.0f}, {4.0f, 0.5f, -1.0f}}, view, 0.1f, 10.0f));
}

TEST(CameraUtilsTest, KeepsInvalidBoundsVisible)
{
    EXPECT_TRUE(IsAABBInsidePerspectiveFace(
        AABB{{1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}}, Matrix4f::Identity(),
        0.1f, 10.0f));
}

TEST(RenderCameraProjection, RoundTripsViewportRaysForBothProjectionModes)
{
    using namespace kpengine;
    for (const auto mode : {render::CameraProjectionMode::Perspective,
                            render::CameraProjectionMode::Orthographic})
    {
        render::RenderCamera camera;
        camera.SetProjectionMode(mode);
        camera.SetPosition({1.0f, 2.0f, 3.0f});
        camera.SetRotation({5.0f, -70.0f, 0.0f});
        camera.SetAspect(0.5f);
        for (const float aspect : {0.75f, 2.0f})
        {
            const auto ray = camera.BuildWorldRay(0.4f, -0.3f, aspect);
            const auto projected = camera.ProjectWorldPoint(ray.origin + ray.direction * 4.0f, aspect);
            ASSERT_TRUE(projected);
            EXPECT_NEAR(projected->x_, 0.4f, 1e-5f);
            EXPECT_NEAR(projected->y_, -0.3f, 1e-5f);
        }
        if (mode == render::CameraProjectionMode::Perspective)
            EXPECT_FALSE(camera.ProjectWorldPoint(camera.GetPosition() - camera.GetForward(), 1.0f));
    }
}

TEST(RenderCameraProjection, RejectsInvalidViewportAndNonfinitePoints)
{
    kpengine::render::RenderCamera camera;
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(camera.ProjectWorldPoint({}, 0.0f));
    EXPECT_FALSE(camera.ProjectWorldPoint({}, invalid));
    EXPECT_FALSE(camera.ProjectWorldPoint({invalid, 0.0f, 0.0f}, 1.0f));
}
