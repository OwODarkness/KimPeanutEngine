#ifndef KPENGINE_RUNTIME_POINT_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_POINT_SHADOW_CASTER_H

#include "shadow_caster.h"
#include <array>
namespace kpengine
{
    struct PointLightData;
    class PointShadowCaster : public ShadowCaster
    {
    public:
        void Initialize() override;
        void AddLight(std::shared_ptr<LightData> light) override;
        void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies) override;
        std::array<Matrix4f, 6> CalculateLighSpaceMatrices(const Vector3f& light_position);
    private:
        std::vector<std::shared_ptr<PointLightData>> lights_;
    };
}

#endif