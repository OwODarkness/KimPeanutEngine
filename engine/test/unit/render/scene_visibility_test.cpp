#include <gtest/gtest.h>

#include "render/render_world/scene_visibility.h"

namespace
{
    using kpengine::Matrix4f;
    using kpengine::render::MeshProxy;
    using kpengine::render::SceneVisibility;

    MeshProxy MakeProxy(float minimum_x, float maximum_x, bool visible = true)
    {
        MeshProxy proxy{};
        proxy.flags.visible = visible;
        proxy.world_bounds = {{minimum_x, -0.25f, -0.25f}, {maximum_x, 0.25f, 0.25f}};
        return proxy;
    }
}

TEST(SceneVisibilityTest, ReturnsOnlyVisibleProxiesIntersectingThePassFrustum)
{
    const std::vector<MeshProxy> proxies{
        MakeProxy(-0.5f, 0.5f),
        MakeProxy(1.1f, 2.0f),
        MakeProxy(-0.5f, 0.5f, false),
    };

    const std::vector<MeshProxy> visible =
        SceneVisibility::BuildVisibleProxies(Matrix4f::Identity(), proxies);

    ASSERT_EQ(visible.size(), 1);
    EXPECT_EQ(visible.front().world_bounds, proxies.front().world_bounds);
}

TEST(SceneVisibilityTest, KeepsMalformedBoundsAsConservativeCandidates)
{
    const std::vector<MeshProxy> proxies{MakeProxy(1.0f, -1.0f)};

    const std::vector<MeshProxy> visible =
        SceneVisibility::BuildVisibleProxies(Matrix4f::Identity(), proxies);

    EXPECT_EQ(visible.size(), 1);
}
