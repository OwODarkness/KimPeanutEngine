#ifndef KPENGINE_RUNTIME_POINT_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_POINT_SHADOW_CASTER_H

#define POINT_MAX_SHADOW_MAP_NUM 4

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
        unsigned int GetShadowMap(unsigned int index) const;
        std::array<unsigned int, POINT_MAX_SHADOW_MAP_NUM> GetShadowMaps() const{return shadow_maps_;}
    private:
        std::vector<std::shared_ptr<PointLightData>> lights_;
        std::array<unsigned int, POINT_MAX_SHADOW_MAP_NUM> fbos_;
        std::array<unsigned int, POINT_MAX_SHADOW_MAP_NUM> shadow_maps_;
    };
}

#endif