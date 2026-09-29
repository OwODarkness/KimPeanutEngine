#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_PASS_UTILS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_PASS_UTILS_H

#include <optional>
#include <vector>

#include "math/math_header.h"
#include "render/light/light_world.h"
#include "render/render_world/scene_visibility.h"

namespace kpengine::render::shadow_pass_utils
{
    struct DirectionalShadowMatrices
    {
        Matrix4f view;
        Matrix4f projection;
    };

    struct DirectionalShadowFit
    {
        spatial::AABB bounds{};
        DirectionalShadowMatrices matrices{};
    };

    std::optional<spatial::AABB> BuildDirectionalShadowCasterBounds(
        const std::vector<VisibleMeshSection> &sections);
    DirectionalShadowMatrices FitDirectionalShadowMatrices(
        const spatial::AABB &caster_bounds, const Vector3f &direction);
    std::optional<DirectionalShadowFit> BuildEffectiveDirectionalShadowFit(
        const std::vector<VisibleMeshSection> &sections,
        const Vector3f &camera_position, const Vector3f &direction);
    bool IsSpotBoundsInsideFrustum(const spatial::AABB &bounds, const Matrix4f &view,
                                   float outer_cone_radians, float near_plane,
                                   float far_plane);
    bool IsBoundsInsideSphere(const spatial::AABB &bounds, const Vector3f &center,
                              float radius);
    uint64_t ComputeDirectionalShadowStamp(
        const Light &light, const std::vector<VisibleMeshSection> &sections,
        const DirectionalShadowFit &fit);
    uint64_t ComputePointShadowStamp(const Light &light,
                                     uint64_t render_world_revision,
                                     uint64_t material_revision);
}

#endif
