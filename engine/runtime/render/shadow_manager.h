#ifndef KPENGINE_RUNTIME_SHADOW_MANAGER_H
#define KPENGINE_RUNTIME_SHADOW_MANAGER_H

#include <memory>
#include <vector>

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

        unsigned int GetDirectionalShadowMap() const;
        unsigned int GetPointShadowMap() const;
        unsigned int GetSpotShadowMap() const;

    private:
        std::unique_ptr<DirectionalShadowCaster> directional_caster_;
        std::unique_ptr<PointShadowCaster> point_caster_;
        std::unique_ptr<SpotShadowCaster> spot_caster_;
    };

}

#endif