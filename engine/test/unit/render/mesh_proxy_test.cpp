#include <gtest/gtest.h>

#include "render/render_world/mesh_proxy.h"

TEST(MeshProxyTest, DefaultsToAnUnregisteredAndResourceFreeSnapshot)
{
    const kpengine::render::MeshProxy proxy{};

    EXPECT_FALSE(proxy.handle.IsValid());
    EXPECT_FALSE(proxy.mesh.IsValid());
    EXPECT_FALSE(proxy.material.IsValid());
    EXPECT_TRUE(proxy.flags.visible);
    EXPECT_TRUE(proxy.flags.opaque);
    EXPECT_TRUE(proxy.flags.casts_shadow);
    EXPECT_EQ(proxy.lod_bias, 0);
}

TEST(MeshProxyTest, UsesSharedSpatialBounds)
{
    const kpengine::spatial::AABB bounds{{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}};
    kpengine::render::MeshProxy proxy{};
    proxy.world_bounds = bounds;

    EXPECT_EQ(proxy.world_bounds, bounds);
}
