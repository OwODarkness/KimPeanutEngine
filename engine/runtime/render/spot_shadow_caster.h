#ifndef KPENGINE_RUNTIME_SPOT_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_SPOT_SHADOW_CASTER_H

#define SPOT_MAX_SHADOW_MAP_NUM 4
#include "shadow_caster.h"
#include "math/math_header.h"
#include<array>
namespace kpengine
{
    struct SpotLightData;
    class SpotShadowCaster : public ShadowCaster
    {
    public:
        void Initialize() override;
        void AddLight(std::shared_ptr<LightData> light) override;
        void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies) override;
        Matrix4f CalculateLighSpaceMatrix(const std::shared_ptr<SpotLightData> &light) const;
        std::array<unsigned int, SPOT_MAX_SHADOW_MAP_NUM> GetShadowMaps() const { return shadow_maps_; }
        std::array<Matrix4f, SPOT_MAX_SHADOW_MAP_NUM> GetLightSpaceMatrices() const{return light_space_matrices;}
    private:
        std::vector<std::shared_ptr<SpotLightData>> lights_;
        std::array<unsigned int, SPOT_MAX_SHADOW_MAP_NUM> fbos_;
        std::array<unsigned int, SPOT_MAX_SHADOW_MAP_NUM> shadow_maps_;
        std::array<Matrix4f, SPOT_MAX_SHADOW_MAP_NUM> light_space_matrices;
    };
}

#endif // KPENGINE_RUNTIME_SPOT_SHADOW_CASTER_H