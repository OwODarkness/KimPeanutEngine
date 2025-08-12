#ifndef KPENGINE_RUNTIME_SHADOW_MANAGER_H
#define KPENGINE_RUNTIME_SHADOW_MANAGER_H

#include <memory>
#include <vector>
#include <array>
#include "runtime/core/math/math_header.h"
namespace kpengine
{
    struct LightData;
    class DirectionalShadowCaster;
    class PointShadowCaster;
    class SpotShadowCaster;
    class PrimitiveSceneProxy;

    class ShadowManager
    {
    public:
        ShadowManager();
        void Initialize();
        void AddLight(std::shared_ptr<LightData>light);
        void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies);
        ~ShadowManager();
        Matrix4f GetDirectionalLightSpaceMatrix() const;
        std::array<Matrix4f, 4> GetSpotLightSpaceMatrix() const;

        unsigned int GetDirectionalShadowMap() const;
        std::array<unsigned int, 4> GetPointShadowMap() const;
        std::array<unsigned int, 4> GetSpotShadowMap() const;

    private:
        std::unique_ptr<DirectionalShadowCaster> directional_caster_;
        std::unique_ptr<PointShadowCaster> point_caster_;
        std::unique_ptr<SpotShadowCaster> spot_caster_;

    };

}

#endif