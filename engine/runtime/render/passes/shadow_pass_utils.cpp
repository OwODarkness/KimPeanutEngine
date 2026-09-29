#include "shadow_pass_utils.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "render/passes/shadow_pass_constants.h"

namespace kpengine::render::shadow_pass_utils
{
    std::optional<spatial::AABB> BuildDirectionalShadowCasterBounds(
        const std::vector<VisibleMeshSection> &sections)
    {
        const float maximum = std::numeric_limits<float>::max();
        spatial::AABB bounds{{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
        bool has_caster = false;
        for (const VisibleMeshSection &candidate : sections)
        {
            const MeshProxy &proxy = candidate.proxy;
            if (!proxy.flags.visible || !proxy.flags.casts_shadow ||
                !candidate.world_bounds.IsValid())
            {
                continue;
            }
            bounds.ExpandToInclude(candidate.world_bounds.min_);
            bounds.ExpandToInclude(candidate.world_bounds.max_);
            has_caster = true;
        }
        return has_caster ? std::optional<spatial::AABB>{bounds} : std::nullopt;
    }

    DirectionalShadowMatrices FitDirectionalShadowMatrices(
        const spatial::AABB &caster_bounds, const Vector3f &direction)
    {
        constexpr float kMargin = 5.0f;
        const Vector3f center = (caster_bounds.min_ + caster_bounds.max_) * 0.5f;
        const Vector3f extent = caster_bounds.max_ - caster_bounds.min_;
        const float radius = 0.5f * std::sqrt(extent.SquareLength());
        const Vector3f up = std::abs(direction.y_) > 0.98f
                                ? Vector3f{0.0f, 0.0f, 1.0f}
                                : Vector3f{0.0f, 1.0f, 0.0f};
        const Matrix4f view = Matrix4f::MakeCameraMatrix(
            center - direction * (radius + kMargin), direction, up);

        const float maximum = std::numeric_limits<float>::max();
        float min_x = maximum;
        float min_y = maximum;
        float min_z = maximum;
        float max_x = -maximum;
        float max_y = -maximum;
        float max_z = -maximum;
        for (const Vector3f &corner : caster_bounds.GetCorners())
        {
            const Vector4f light_space = view * Vector4f{corner, 1.0f};
            min_x = std::min(min_x, light_space.x_);
            min_y = std::min(min_y, light_space.y_);
            min_z = std::min(min_z, light_space.z_);
            max_x = std::max(max_x, light_space.x_);
            max_y = std::max(max_y, light_space.y_);
            max_z = std::max(max_z, light_space.z_);
        }

        const float near_plane = std::max(0.1f, -max_z - kMargin);
        const float far_plane = std::max(near_plane + 1.0f, -min_z + kMargin);
        return {view, Matrix4f::MakeOrthProjMatrix(
                          min_x - kMargin, max_x + kMargin, min_y - kMargin,
                          max_y + kMargin, near_plane, far_plane)};
    }

    namespace
    {
        bool IsPointInsideDirectionalShadowFit(const DirectionalShadowMatrices &matrices,
                                               const Vector3f &point)
        {
            constexpr float kContainmentEpsilon = 1.0e-4f;
            const Vector4f clip = matrices.projection * (matrices.view * Vector4f{point, 1.0f});
            return std::abs(clip.x_) <= 1.0f + kContainmentEpsilon &&
                   std::abs(clip.y_) <= 1.0f + kContainmentEpsilon &&
                   std::abs(clip.z_) <= 1.0f + kContainmentEpsilon;
        }
    }

    std::optional<DirectionalShadowFit> BuildEffectiveDirectionalShadowFit(
        const std::vector<VisibleMeshSection> &sections,
        const Vector3f &camera_position, const Vector3f &direction)
    {
        const std::optional<spatial::AABB> caster_bounds =
            BuildDirectionalShadowCasterBounds(sections);
        if (!caster_bounds.has_value())
        {
            return std::nullopt;
        }

        DirectionalShadowFit fit{*caster_bounds,
                                 FitDirectionalShadowMatrices(*caster_bounds, direction)};
        if (!IsPointInsideDirectionalShadowFit(fit.matrices, camera_position))
        {
            fit.bounds.ExpandToInclude(camera_position);
            fit.matrices = FitDirectionalShadowMatrices(fit.bounds, direction);
        }
        return fit;
    }

    bool IsSpotBoundsInsideFrustum(const spatial::AABB &bounds, const Matrix4f &view,
                                   float outer_cone_radians, float near_plane,
                                   float far_plane)
    {
        if (!bounds.IsValid())
        {
            return true;
        }
        const float tangent = std::tan(outer_cone_radians);
        for (const Vector3f &corner : bounds.GetCorners())
        {
            const Vector4f light_space = view * Vector4f{corner, 1.0f};
            const float depth = -light_space.z_;
            if (depth >= near_plane && depth <= far_plane &&
                std::abs(light_space.x_) <= depth * tangent &&
                std::abs(light_space.y_) <= depth * tangent)
            {
                return true;
            }
        }
        return false;
    }

    bool IsBoundsInsideSphere(const spatial::AABB &bounds, const Vector3f &center,
                              float radius)
    {
        if (!bounds.IsValid())
        {
            return true;
        }
        const Vector3f closest{std::max(bounds.min_.x_, std::min(center.x_, bounds.max_.x_)),
                               std::max(bounds.min_.y_, std::min(center.y_, bounds.max_.y_)),
                               std::max(bounds.min_.z_, std::min(center.z_, bounds.max_.z_))};
        const Vector3f delta = closest - center;
        return delta.SquareLength() <= radius * radius;
    }

    uint64_t ComputeDirectionalShadowStamp(
        const Light &light, const std::vector<VisibleMeshSection> &sections,
        const DirectionalShadowFit &fit)
    {
        uint64_t stamp = 1469598103934665603ULL;
        const auto add = [&stamp](uint64_t value) {
            stamp ^= value;
            stamp *= 1099511628211ULL;
        };
        const auto add_float = [&add](float value) {
            add(static_cast<uint64_t>(std::hash<float>{}(value)));
        };
        const auto add_vector = [&add_float](const Vector3f &value) {
            add_float(value.x_);
            add_float(value.y_);
            add_float(value.z_);
        };
        add(light.handle.id);
        add(light.handle.generation);
        add(light.desc.shadow->id);
        add(light.desc.shadow->generation);
        add_vector(std::get<DirectionalLightData>(light.desc.type_data).direction);
        add(fit.bounds.IsValid() ? 1u : 0u);
        if (fit.bounds.IsValid())
        {
            add_vector(fit.bounds.min_);
            add_vector(fit.bounds.max_);
        }
        const auto add_matrix = [&add_float](const Matrix4f &matrix) {
            for (size_t row = 0; row < 4; ++row)
            {
                for (size_t column = 0; column < 4; ++column)
                {
                    add_float(matrix[row][column]);
                }
            }
        };
        add_matrix(fit.matrices.view);
        add_matrix(fit.matrices.projection);
        for (const VisibleMeshSection &candidate : sections)
        {
            const MeshProxy &proxy = candidate.proxy;
            add(proxy.handle.id);
            add(proxy.handle.generation);
            add(proxy.mesh.id);
            add(proxy.mesh.generation);
            add(proxy.material.id);
            add(proxy.material.generation);
            add(candidate.section_index);
            add(proxy.flags.visible ? 1u : 0u);
            add(proxy.flags.casts_shadow ? 1u : 0u);
            add_vector(proxy.world_transform.position_);
            add_vector(proxy.world_transform.scale_);
            add_float(proxy.world_transform.rotator_.pitch_);
            add_float(proxy.world_transform.rotator_.yaw_);
            add_float(proxy.world_transform.rotator_.roll_);
            add_vector(candidate.world_bounds.min_);
            add_vector(candidate.world_bounds.max_);
        }
        return stamp;
    }

    uint64_t ComputePointShadowStamp(const Light &light,
                                     uint64_t render_world_revision,
                                     uint64_t material_revision)
    {
        uint64_t stamp = 1469598103934665603ULL;
        const auto add = [&stamp](uint64_t value) {
            stamp ^= value;
            stamp *= 1099511628211ULL;
        };
        const auto add_float = [&add](float value) {
            add(static_cast<uint64_t>(std::hash<float>{}(value)));
        };
        const PointLightData &point = std::get<PointLightData>(light.desc.type_data);
        add(light.handle.id);
        add(light.handle.generation);
        add(light.desc.shadow->id);
        add(light.desc.shadow->generation);
        add(render_world_revision);
        add(material_revision);
        add(shadow_pass_detail::kPointShadowFaceResolution);
        add_float(point.position.x_);
        add_float(point.position.y_);
        add_float(point.position.z_);
        add_float(point.range);
        return stamp;
    }
}
