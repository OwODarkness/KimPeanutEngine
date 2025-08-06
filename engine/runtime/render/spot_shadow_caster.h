#ifndef KPENGINE_RUNTIME_SPOT_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_SPOT_SHADOW_CASTER_H

#include "shadow_caster.h"
namespace kpengine
{
    class SpotShadowCaster : public ShadowCaster
    {
    public:
        void Initialize() override;
        void AddLight(std::shared_ptr<LightData>light) override;
        void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies) override;
    };
}

#endif//KPENGINE_RUNTIME_SPOT_SHADOW_CASTER_H