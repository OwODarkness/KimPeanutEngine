#include "gameplay/component/point_light_component.h"

#include "gameplay/actor/actor.h"

namespace kpengine::gameplay
{
    void PointLightComponent::SetColor(const Vector3f &color)
    {
        if (color_ != color)
        {
            color_ = color;
            MarkSourceDirty();
        }
    }

    void PointLightComponent::SetIntensity(float intensity)
    {
        if (intensity_ != intensity)
        {
            intensity_ = intensity;
            MarkSourceDirty();
        }
    }

    void PointLightComponent::SetRange(float range)
    {
        if (range_ != range)
        {
            range_ = range;
            MarkSourceDirty();
        }
    }

    void PointLightComponent::SetLightEnabled(bool enabled)
    {
        if (enabled_ != enabled)
        {
            enabled_ = enabled;
            MarkSourceDirty();
        }
    }

    void PointLightComponent::SetCastsShadow(bool casts_shadow)
    {
        if (casts_shadow_ != casts_shadow)
        {
            casts_shadow_ = casts_shadow;
            MarkSourceDirty();
        }
    }

    void PointLightComponent::OnActivate()
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

    void PointLightComponent::OnDeactivate()
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

    void PointLightComponent::OnTick(float delta_time)
    {
        SceneComponent::OnTick(delta_time);
        FlushSourceUpdate();
    }

    void PointLightComponent::OnTransformChanged()
    {
        MarkSourceDirty();
    }

    render::LightSourceDesc PointLightComponent::BuildSourceDesc() const
    {
        render::PointLightSourceDesc source{};
        source.position = GetWorldTransform().position_;
        source.color = color_;
        source.intensity = intensity_;
        source.range = range_;
        source.enabled = enabled_;
        source.casts_shadow = casts_shadow_;
        return source;
    }

    void PointLightComponent::MarkSourceDirty()
    {
        source_dirty_ = true;
    }

    void PointLightComponent::FlushSourceUpdate()
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
