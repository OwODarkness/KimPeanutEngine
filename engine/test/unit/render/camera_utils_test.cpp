#include <gtest/gtest.h>

#include "render/camera_utils.h"

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
