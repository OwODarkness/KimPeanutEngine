#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_DIRECTIONAL_LIGHT_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_DIRECTIONAL_LIGHT_COMPONENT_H

#include "gameplay/component/scene_component.h"
#include "render/light/light_source.h"

namespace kpengine::gameplay
{
    // Gameplay-owned directional-light authoring state. The retained token is
    // only a source registration; Render resolves it later into private state.
    class DirectionalLightComponent final : public SceneComponent
    {
    public:
        const Vector3f &GetColor() const { return color_; }
        float GetIntensity() const { return intensity_; }
        bool IsLightEnabled() const { return enabled_; }
        bool CastsShadow() const { return casts_shadow_; }
        render::LightSourceHandle GetSourceHandle() const { return source_handle_; }

        void SetColor(const Vector3f &color);
        void SetIntensity(float intensity);
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

        Vector3f color_{1.0f, 1.0f, 1.0f};
        float intensity_ = 1.0f;
        bool enabled_ = true;
        bool casts_shadow_ = true;
        bool source_dirty_ = true;
        render::LightSourceHandle source_handle_;
    };
}

#endif
