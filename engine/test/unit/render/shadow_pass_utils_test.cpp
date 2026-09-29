#include <gtest/gtest.h>

#include "render/passes/shadow_pass_utils.h"

namespace
{
    using namespace kpengine;
    using namespace kpengine::render;
    using namespace kpengine::render::shadow_pass_utils;
    using kpengine::spatial::AABB;

    VisibleMeshSection MakeSection(AABB bounds, bool visible = true,
                                   bool casts_shadow = true)
    {
        VisibleMeshSection section{};
        section.proxy.flags.visible = visible;
        section.proxy.flags.casts_shadow = casts_shadow;
        section.world_bounds = bounds;
        return section;
    }
}

TEST(ShadowPassUtilsTest, FitsBoundsFromVisibleShadowCastersOnly)
{
    const std::vector<VisibleMeshSection> sections{
        MakeSection({{-2.0f, -1.0f, -3.0f}, {2.0f, 1.0f, 3.0f}}),
        MakeSection({{-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f}}, false),
        MakeSection({{-50.0f, -50.0f, -50.0f}, {50.0f, 50.0f, 50.0f}}, true, false),
    };

    const auto bounds = BuildDirectionalShadowCasterBounds(sections);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(*bounds, (AABB{{-2.0f, -1.0f, -3.0f}, {2.0f, 1.0f, 3.0f}}));
    EXPECT_FALSE(BuildDirectionalShadowCasterBounds({}).has_value());
}

TEST(ShadowPassUtilsTest, ExtendsDirectionalFitToIncludeCameraPosition)
{
    const std::vector<VisibleMeshSection> sections{
        MakeSection({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}})};
    const Vector3f camera_position{30.0f, 8.0f, 12.0f};

    const auto fit = BuildEffectiveDirectionalShadowFit(
        sections, camera_position, Vector3f{0.0f, -1.0f, 0.0f});
    ASSERT_TRUE(fit.has_value());
    EXPECT_LE(fit->bounds.min_.x_, camera_position.x_);
    EXPECT_GE(fit->bounds.max_.x_, camera_position.x_);
    EXPECT_LE(fit->bounds.min_.y_, camera_position.y_);
    EXPECT_GE(fit->bounds.max_.y_, camera_position.y_);
    EXPECT_LE(fit->bounds.min_.z_, camera_position.z_);
    EXPECT_GE(fit->bounds.max_.z_, camera_position.z_);
}

TEST(ShadowPassUtilsTest, UsesConservativeSpotAndPointLightVolumeTests)
{
    const Matrix4f view = Matrix4f::Identity();
    EXPECT_TRUE(IsSpotBoundsInsideFrustum(
        {{-0.1f, -0.1f, -3.0f}, {0.1f, 0.1f, -2.0f}}, view,
        0.5f, 0.1f, 10.0f));
    EXPECT_FALSE(IsSpotBoundsInsideFrustum(
        {{10.0f, 10.0f, -3.0f}, {11.0f, 11.0f, -2.0f}}, view,
        0.5f, 0.1f, 10.0f));
    const AABB invalid_bounds{{1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}};
    EXPECT_TRUE(IsSpotBoundsInsideFrustum(invalid_bounds, view, 0.5f, 0.1f, 10.0f));

    EXPECT_TRUE(IsBoundsInsideSphere(
        {{-0.1f, -0.1f, -0.1f}, {0.1f, 0.1f, 0.1f}}, {}, 1.0f));
    EXPECT_FALSE(IsBoundsInsideSphere(
        {{2.0f, 2.0f, 2.0f}, {3.0f, 3.0f, 3.0f}}, {}, 1.0f));
    EXPECT_TRUE(IsBoundsInsideSphere(invalid_bounds, {}, 1.0f));
}

TEST(ShadowPassUtilsTest, ShadowStampsTrackSceneAndLightRevisions)
{
    Light point{};
    point.handle = {1, 2};
    point.desc.type = LightType::Point;
    point.desc.shadow = ShadowHandle{3, 4};
    point.desc.type_data = PointLightData{{1.0f, 2.0f, 3.0f}, 5.0f};

    const uint64_t point_stamp = ComputePointShadowStamp(point, 10, 20);
    EXPECT_EQ(ComputePointShadowStamp(point, 10, 20), point_stamp);
    EXPECT_NE(ComputePointShadowStamp(point, 11, 20), point_stamp);
    EXPECT_NE(ComputePointShadowStamp(point, 10, 21), point_stamp);

    Light directional{};
    directional.handle = {7, 8};
    directional.desc.type = LightType::Directional;
    directional.desc.shadow = ShadowHandle{9, 10};
    directional.desc.type_data = DirectionalLightData{{0.0f, -1.0f, 0.0f}};
    const std::vector<VisibleMeshSection> sections{
        MakeSection({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}})};
    const auto fit = BuildEffectiveDirectionalShadowFit(
        sections, {}, Vector3f{0.0f, -1.0f, 0.0f});
    ASSERT_TRUE(fit.has_value());
    const uint64_t directional_stamp =
        ComputeDirectionalShadowStamp(directional, sections, *fit);
    EXPECT_EQ(ComputeDirectionalShadowStamp(directional, sections, *fit),
              directional_stamp);
    EXPECT_NE(ComputeDirectionalShadowStamp(directional, {}, *fit),
              directional_stamp);
}
