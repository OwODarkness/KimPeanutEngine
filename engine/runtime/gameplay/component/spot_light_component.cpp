#include "gameplay/component/spot_light_component.h"

#include "gameplay/actor/actor.h"

namespace kpengine::gameplay
{
    void SpotLightComponent::SetPosition(const Vector3f &position)
    {
        if (position_ != position)
        {
            position_ = position;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetDirection(const Vector3f &direction)
    {
        if (direction_ != direction)
        {
            direction_ = direction;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetColor(const Vector3f &color)
    {
        if (color_ != color)
        {
            color_ = color;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetIntensity(float intensity)
    {
        if (intensity_ != intensity)
        {
            intensity_ = intensity;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetRange(float range)
    {
        if (range_ != range)
        {
            range_ = range;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetInnerConeRadians(float radians)
    {
        if (inner_cone_radians_ != radians)
        {
            inner_cone_radians_ = radians;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetOuterConeRadians(float radians)
    {
        if (outer_cone_radians_ != radians)
        {
            outer_cone_radians_ = radians;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetLightEnabled(bool enabled)
    {
        if (enabled_ != enabled)
        {
            enabled_ = enabled;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::SetCastsShadow(bool casts_shadow)
    {
        if (casts_shadow_ != casts_shadow)
        {
            casts_shadow_ = casts_shadow;
            MarkSourceDirty();
        }
    }

    void SpotLightComponent::OnActivate()
    {
        SceneComponent::OnActivate();
        Actor *const owner = GetOwner();
        render::ILightSourceSink *const source_sink =
            owner != nullptr ? owner->GetLightSourceSink() : nullptr;
        if (source_sink != nullptr)
        {
            source_handle_ = source_sink->EnqueueCreate(BuildSourceDesc());
        }
        source_dirty_ = false;
    }

    void SpotLightComponent::OnDeactivate()
    {
        Actor *const owner = GetOwner();
        render::ILightSourceSink *const source_sink =
            owner != nullptr ? owner->GetLightSourceSink() : nullptr;
        if (source_sink != nullptr && source_handle_.IsValid())
        {
            (void)source_sink->EnqueueDestroy(source_handle_);
        }
        source_handle_ = {};
        source_dirty_ = true;
        SceneComponent::OnDeactivate();
    }

    void SpotLightComponent::OnTick(float delta_time)
    {
        SceneComponent::OnTick(delta_time);
        FlushSourceUpdate();
    }

    render::LightSourceDesc SpotLightComponent::BuildSourceDesc() const
    {
        render::SpotLightSourceDesc source{};
        source.position = position_;
        source.direction = direction_;
        source.color = color_;
        source.intensity = intensity_;
        source.range = range_;
        source.inner_cone_radians = inner_cone_radians_;
        source.outer_cone_radians = outer_cone_radians_;
        source.enabled = enabled_;
        source.casts_shadow = casts_shadow_;
        return source;
    }

    void SpotLightComponent::MarkSourceDirty()
    {
        source_dirty_ = true;
    }

    void SpotLightComponent::FlushSourceUpdate()
    {
        if (!source_dirty_ || !source_handle_.IsValid())
        {
            return;
        }
        Actor *const owner = GetOwner();
        render::ILightSourceSink *const source_sink =
            owner != nullptr ? owner->GetLightSourceSink() : nullptr;
        if (source_sink != nullptr)
        {
            (void)source_sink->EnqueueUpdate(source_handle_, BuildSourceDesc());
        }
        source_dirty_ = false;
    }
}
