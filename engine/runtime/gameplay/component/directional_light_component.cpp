#include "gameplay/component/directional_light_component.h"

#include "gameplay/actor/actor.h"

namespace kpengine::gameplay
{
    void DirectionalLightComponent::SetDirection(const Vector3f &direction)
    {
        if (direction_ != direction)
        {
            direction_ = direction;
            MarkSourceDirty();
        }
    }

    void DirectionalLightComponent::SetColor(const Vector3f &color)
    {
        if (color_ != color)
        {
            color_ = color;
            MarkSourceDirty();
        }
    }

    void DirectionalLightComponent::SetIntensity(float intensity)
    {
        if (intensity_ != intensity)
        {
            intensity_ = intensity;
            MarkSourceDirty();
        }
    }

    void DirectionalLightComponent::SetLightEnabled(bool enabled)
    {
        if (enabled_ != enabled)
        {
            enabled_ = enabled;
            MarkSourceDirty();
        }
    }

    void DirectionalLightComponent::SetCastsShadow(bool casts_shadow)
    {
        if (casts_shadow_ != casts_shadow)
        {
            casts_shadow_ = casts_shadow;
            MarkSourceDirty();
        }
    }

    void DirectionalLightComponent::OnActivate()
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

    void DirectionalLightComponent::OnDeactivate()
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

    void DirectionalLightComponent::OnTick(float delta_time)
    {
        SceneComponent::OnTick(delta_time);
        FlushSourceUpdate();
    }

    render::LightSourceDesc DirectionalLightComponent::BuildSourceDesc() const
    {
        render::DirectionalLightSourceDesc source{};
        source.direction = direction_;
        source.color = color_;
        source.intensity = intensity_;
        source.enabled = enabled_;
        source.casts_shadow = casts_shadow_;
        return source;
    }

    void DirectionalLightComponent::MarkSourceDirty()
    {
        source_dirty_ = true;
    }

    void DirectionalLightComponent::FlushSourceUpdate()
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
