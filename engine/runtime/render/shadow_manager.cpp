#include "shadow_manager.h"

#include "directional_shadow_caster.h"
#include "point_shadow_caster.h"
#include "spot_shadow_caster.h"
#include "render_light.h"

namespace kpengine
{
    ShadowManager::ShadowManager() : directional_caster_(std::make_unique<DirectionalShadowCaster>()),
                                     point_caster_(std::make_unique<PointShadowCaster>()),
                                     spot_caster_(std::make_unique<SpotShadowCaster>())
    {
    }

    void ShadowManager::Initialize()
    {
        directional_caster_->Initialize();
        point_caster_->Initialize();
        spot_caster_->Initialize();
    }

    void ShadowManager::AddLight(std::shared_ptr<LightData> light)
    {
        if (!light)
        {
            return;
        }
        switch (light->light_type)
        {
        case LightType::Directional:
            directional_caster_->AddLight(light);
            break;
        case LightType::Point:
            point_caster_->AddLight(light);
            break;
        case LightType::Spot:
            spot_caster_->AddLight(light);
            break;
        default:
            break;
        }
    }

    void ShadowManager::Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies)
    {
        directional_caster_->Render(proxies);
        point_caster_->Render(proxies);
        spot_caster_->Render(proxies);
    }

    unsigned int ShadowManager::GetDirectionalShadowMap() const
    {
        return directional_caster_->GetShadowMap();
    }
    std::array<unsigned int, 4> ShadowManager::GetPointShadowMap() const
    {
        return point_caster_->GetShadowMaps();
    }
    std::array<unsigned int, 4> ShadowManager::GetSpotShadowMap() const
    {
        return spot_caster_->GetShadowMaps();
    }

    Matrix4f ShadowManager::GetDirectionalLightSpaceMatrix() const
    {
        return directional_caster_->CalculateLighSpaceMatrix();
    }

    std::array<Matrix4f, 4> ShadowManager::GetSpotLightSpaceMatrix() const
    {
        return spot_caster_->GetLightSpaceMatrices();
    }


    ShadowManager::~ShadowManager() = default;

}