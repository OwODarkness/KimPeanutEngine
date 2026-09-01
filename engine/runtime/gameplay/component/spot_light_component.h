#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_SPOT_LIGHT_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_SPOT_LIGHT_COMPONENT_H

#include "gameplay/component/scene_component.h"
#include "render/light/light_source.h"

namespace kpengine::gameplay
{
    // Gameplay-owned spot-light authoring state. It publishes only copied
    // values; Render owns all later Spot2D target and scheduling policy.
    class SpotLightComponent final : public SceneComponent
    {
    public:
        const Vector3f &GetPosition() const { return position_; }
        const Vector3f &GetDirection() const { return direction_; }
        const Vector3f &GetColor() const { return color_; }
        float GetIntensity() const { return intensity_; }
        float GetRange() const { return range_; }
        float GetInnerConeRadians() const { return inner_cone_radians_; }
        float GetOuterConeRadians() const { return outer_cone_radians_; }
        bool IsLightEnabled() const { return enabled_; }
        bool CastsShadow() const { return casts_shadow_; }
        render::LightSourceHandle GetSourceHandle() const { return source_handle_; }

        void SetPosition(const Vector3f &position);
        void SetDirection(const Vector3f &direction);
        void SetColor(const Vector3f &color);
        void SetIntensity(float intensity);
        void SetRange(float range);
        void SetInnerConeRadians(float radians);
        void SetOuterConeRadians(float radians);
        void SetLightEnabled(bool enabled);
        void SetCastsShadow(bool casts_shadow);

    protected:
        void OnActivate() override;
        void OnDeactivate() override;
        void OnTick(float delta_time) override;

    private:
        render::LightSourceDesc BuildSourceDesc() const;
        void MarkSourceDirty();
        void FlushSourceUpdate();

        Vector3f position_{};
        Vector3f direction_{0.0f, -1.0f, 0.0f};
        Vector3f color_{1.0f, 1.0f, 1.0f};
        float intensity_ = 1.0f;
        float range_ = 1.0f;
        float inner_cone_radians_ = 0.0f;
        float outer_cone_radians_ = 0.785398163f;
        bool enabled_ = true;
        bool casts_shadow_ = true;
        bool source_dirty_ = true;
        render::LightSourceHandle source_handle_;
    };
}

#endif
