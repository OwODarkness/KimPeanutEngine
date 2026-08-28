#include "gameplay/component/primitive_component.h"

#include <algorithm>
#include <array>
#include <limits>

namespace kpengine::gameplay
{
    void PrimitiveComponent::SetVisible(bool visible)
    {
        if (visible_ != visible)
        {
            visible_ = visible;
            OnPrimitiveStateChanged();
        }
    }

    void PrimitiveComponent::SetCastsShadow(bool casts_shadow)
    {
        if (casts_shadow_ != casts_shadow)
        {
            casts_shadow_ = casts_shadow;
            OnPrimitiveStateChanged();
        }
    }

    void PrimitiveComponent::SetLocalBounds(const spatial::AABB &bounds)
    {
        if (local_bounds_ != bounds)
        {
            local_bounds_ = bounds;
            OnPrimitiveStateChanged();
        }
    }

    spatial::AABB PrimitiveComponent::GetWorldBounds() const
    {
        if (!local_bounds_.IsValid())
        {
            return local_bounds_;
        }

        const Transform3f &transform = GetWorldTransform();
        const std::array<Vector3f, 8> corners{{
            {local_bounds_.min_.x_, local_bounds_.min_.y_, local_bounds_.min_.z_},
            {local_bounds_.min_.x_, local_bounds_.min_.y_, local_bounds_.max_.z_},
            {local_bounds_.min_.x_, local_bounds_.max_.y_, local_bounds_.min_.z_},
            {local_bounds_.min_.x_, local_bounds_.max_.y_, local_bounds_.max_.z_},
            {local_bounds_.max_.x_, local_bounds_.min_.y_, local_bounds_.min_.z_},
            {local_bounds_.max_.x_, local_bounds_.min_.y_, local_bounds_.max_.z_},
            {local_bounds_.max_.x_, local_bounds_.max_.y_, local_bounds_.min_.z_},
            {local_bounds_.max_.x_, local_bounds_.max_.y_, local_bounds_.max_.z_},
        }};

        const float maximum = std::numeric_limits<float>::max();
        spatial::AABB world_bounds{{maximum, maximum, maximum},
                                   {-maximum, -maximum, -maximum}};
        for (const Vector3f &corner : corners)
        {
            const Vector3f transformed =
                transform.rotator_.RotateVector(transform.scale_ * corner) + transform.position_;
            world_bounds.min_.x_ = std::min(world_bounds.min_.x_, transformed.x_);
            world_bounds.min_.y_ = std::min(world_bounds.min_.y_, transformed.y_);
            world_bounds.min_.z_ = std::min(world_bounds.min_.z_, transformed.z_);
            world_bounds.max_.x_ = std::max(world_bounds.max_.x_, transformed.x_);
            world_bounds.max_.y_ = std::max(world_bounds.max_.y_, transformed.y_);
            world_bounds.max_.z_ = std::max(world_bounds.max_.z_, transformed.z_);
        }
        return world_bounds;
    }
}
