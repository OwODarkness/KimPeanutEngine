#include "gameplay/component/primitive_component.h"

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
        return spatial::TransformAABB(local_bounds_, GetWorldTransform());
    }
}
