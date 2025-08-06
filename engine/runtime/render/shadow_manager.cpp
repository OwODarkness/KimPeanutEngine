#include "shadow_manager.h"

#include "runtime/render/directional_shadow_caster.h"
#include "runtime/render/point_shadow_caster.h"
#include "runtime/render/spot_shadow_caster.h"
#include "runtime/render/render_light.h"

namespace kpengine{
    ShadowManager::ShadowManager():
    directional_caster_(std::make_unique<DirectionalShadowCaster>()),
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

    void ShadowManager::AddLight(std::shared_ptr<LightData>light)
    {
        if(!light)
        {
            return ;
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
        unsigned int ShadowManager::GetPointShadowMap() const
        {
            return point_caster_->GetShadowMap();
        }
        unsigned int ShadowManager::GetSpotShadowMap() const
        {
            return spot_caster_->GetShadowMap();
        }

    ShadowManager::~ShadowManager() = default;

}