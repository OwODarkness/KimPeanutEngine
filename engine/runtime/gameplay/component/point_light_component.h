#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_POINT_LIGHT_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_POINT_LIGHT_COMPONENT_H

#include "gameplay/component/scene_component.h"
#include "render/light/light_source.h"

namespace kpengine::gameplay
{
    // Gameplay-owned local light; optional half axes select a rectangular emitter.
    // Render retains shadow target and scheduling ownership.
    class PointLightComponent final : public SceneComponent
    {
    public:
        const Vector3f &GetColor() const { return color_; }
        float GetIntensity() const { return intensity_; }
        float GetRange() const { return range_; }
        bool IsLightEnabled() const { return enabled_; }
        bool CastsShadow() const { return casts_shadow_; }
        render::LightSourceHandle GetSourceHandle() const { return source_handle_; }

        void SetColor(const Vector3f &color);
        void SetIntensity(float intensity);
        void SetRange(float range);
        void SetAreaHalfAxes(const Vector3f &u, const Vector3f &v);
        void SetLightEnabled(bool enabled);
        void SetCastsShadow(bool casts_shadow);

    protected:
        void OnActivate() override;
        void OnDeactivate() override;
        void OnTick(float delta_time) override;
        void OnTransformChanged() override;

    private:
        render::LightSourceDesc BuildSourceDesc() const;
        void MarkSourceDirty();
        void FlushSourceUpdate();

        Vector3f half_axis_u_{};
        Vector3f half_axis_v_{};
        Vector3f color_{1.0f, 1.0f, 1.0f};
        float intensity_ = 1.0f;
        float range_ = 1.0f;
        bool enabled_ = true;
        bool casts_shadow_ = true;
        bool source_dirty_ = true;
        render::LightSourceHandle source_handle_;
    };
}

#endif
