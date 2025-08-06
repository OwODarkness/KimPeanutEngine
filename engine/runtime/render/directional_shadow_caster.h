#ifndef KPENGINE_RUNTIME_DIRECTIONAL_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_DIRECTIONAL_SHADOW_CASTER_H

#include "shadow_caster.h"

namespace kpengine{

    struct DirectionalLightData;
    class DirectionalShadowCaster: public ShadowCaster{
    public:
        void Initialize() override;
        void AddLight(std::shared_ptr<LightData> light) override;
        void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies) override;
    private:
        Matrix4f CalculateLighSpaceMatrix(const Vector3f& light_position);
    private:
        std::shared_ptr<DirectionalLightData> light_;
    };
}

#endif