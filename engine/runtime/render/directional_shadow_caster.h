#ifndef KPENGINE_RUNTIME_DIRECTIONAL_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_DIRECTIONAL_SHADOW_CASTER_H

#include "shadow_caster.h"

namespace kpengine
{

    struct DirectionalLightData;
    class DirectionalShadowCaster : public ShadowCaster
    {
    public:
        void Initialize() override;
        void AddLight(std::shared_ptr<LightData> light) override;
        void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies) override;
        unsigned int GetShadowMap() const{return shadow_map_;}
    public:
        Matrix4f CalculateLighSpaceMatrix() const;

    private:
        std::shared_ptr<DirectionalLightData> light_;
        unsigned int fbo_;
        unsigned int shadow_map_;
    };
}

#endif