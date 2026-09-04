#include <gtest/gtest.h>

#include "spatial/aabb.h"

namespace
{
    using kpengine::Transform3f;
    using kpengine::Vector3f;
    using kpengine::spatial::AABB;
    using kpengine::spatial::TransformAABB;
}

TEST(AABBTest, EnumeratesCornersAndExpandsToIncludePoints)
{
    AABB bounds{{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}};

    const auto corners = bounds.GetCorners();

    EXPECT_EQ(corners.front(), (Vector3f{-1.0f, -2.0f, -3.0f}));
    EXPECT_EQ(corners.back(), (Vector3f{1.0f, 2.0f, 3.0f}));

    bounds.ExpandToInclude({-4.0f, 0.0f, 5.0f});
    EXPECT_EQ(bounds, (AABB{{-4.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 5.0f}}));
}

TEST(AABBTest, TransformsLocalBoundsIntoWorldSpace)
{
    const AABB local_bounds{{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}};
    const Transform3f transform{{10.0f, 20.0f, 30.0f}, {}, {2.0f, 3.0f, 4.0f}};

    EXPECT_EQ(TransformAABB(local_bounds, transform),
              (AABB{{8.0f, 14.0f, 18.0f}, {12.0f, 26.0f, 42.0f}}));
}

TEST(AABBTest, PreservesInvalidBoundsWhenTransforming)
{
    const AABB invalid{{1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}};

    EXPECT_EQ(TransformAABB(invalid, Transform3f{}), invalid);
}
