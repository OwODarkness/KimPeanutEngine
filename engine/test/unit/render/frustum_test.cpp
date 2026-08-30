#include <gtest/gtest.h>

#include "render/render_camera.h"
#include "render/render_world/frustum.h"

namespace
{
    using kpengine::Matrix4f;
    using kpengine::render::Frustum;
    using kpengine::render::RenderCamera;
    using kpengine::spatial::AABB;
}

TEST(FrustumTest, IdentityViewProjectionClassifiesAabbsConservatively)
{
    const Frustum frustum = Frustum::FromViewProjection(Matrix4f::Identity());

    EXPECT_TRUE(frustum.Intersects(AABB{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}));
    EXPECT_TRUE(frustum.Intersects(AABB{{0.8f, -0.1f, -0.1f}, {1.2f, 0.1f, 0.1f}}));
    EXPECT_FALSE(frustum.Intersects(AABB{{1.1f, -0.1f, -0.1f}, {2.0f, 0.1f, 0.1f}}));
}

TEST(FrustumTest, KeepsInvalidBoundsVisibleAsTheSafeFallback)
{
    const Frustum frustum = Frustum::FromViewProjection(Matrix4f::Identity());
    EXPECT_TRUE(frustum.Intersects(AABB{{1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f}}));
}

TEST(FrustumTest, UsesTheCameraMathConventionRatherThanGpuTransposedMatrices)
{
    RenderCamera camera{};
    const Frustum frustum = Frustum::FromViewProjection(camera.GetViewProjectionMatrix());

    EXPECT_TRUE(frustum.Intersects(AABB{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}));
    EXPECT_FALSE(frustum.Intersects(AABB{{-0.5f, -0.5f, 3.0f}, {0.5f, 0.5f, 4.0f}}));
    EXPECT_FALSE(frustum.Intersects(AABB{{-0.5f, -0.5f, -20.0f}, {0.5f, 0.5f, -19.0f}}));
}

TEST(FrustumTest, ProjectionMatricesPreserveEngineYForBackendViewportNormalization)
{
    const Matrix4f perspective = Matrix4f::MakePerProjMatrix(
        kpengine::math::DegreeToRadian(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Matrix4f orthographic = Matrix4f::MakeOrthProjMatrix(
        -8.0f, 8.0f, -4.5f, 4.5f, 0.1f, 100.0f);

    EXPECT_GT(perspective[1][1], 0.0f);
    EXPECT_GT(orthographic[1][1], 0.0f);
}
