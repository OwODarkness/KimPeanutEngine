#include "gameplay/scene_transform_utils.h"

#include <algorithm>
#include <cmath>

namespace kpengine::gameplay
{
    Vector3f GetSceneForwardDirection(const Rotatorf &rotation)
    {
        const float pitch = math::DegreeToRadian(rotation.pitch_);
        const float yaw = math::DegreeToRadian(rotation.yaw_);
        return {std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                std::cos(pitch) * std::sin(yaw)};
    }

    bool TryMakeSceneForwardRotation(const Vector3f &direction, Rotatorf &rotation)
    {
        const float square_length = direction.SquareLength();
        if (!std::isfinite(direction.x_) || !std::isfinite(direction.y_) ||
            !std::isfinite(direction.z_) || !std::isfinite(square_length) ||
            square_length <= 0.0f)
        {
            return false;
        }

        const float inverse_length = 1.0f / std::sqrt(square_length);
        const Vector3f normalized = direction * inverse_length;
        const float clamped_y = std::clamp(normalized.y_, -1.0f, 1.0f);
        rotation = {math::RadianToDegree(std::asin(clamped_y)),
                    math::RadianToDegree(std::atan2(normalized.z_, normalized.x_)), 0.0f};
        return std::isfinite(rotation.pitch_) && std::isfinite(rotation.yaw_);
    }
}
