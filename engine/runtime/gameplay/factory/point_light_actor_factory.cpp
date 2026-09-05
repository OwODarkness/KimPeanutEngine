#include "gameplay/factory/point_light_actor_factory.h"

#include "gameplay/actor/actor.h"
#include "gameplay/component/point_light_component.h"
#include "gameplay/world/gameplay_world.h"

namespace kpengine::gameplay
{
    ActorHandle CreatePointLightActor(GameplayWorld &world, const PointLightActorDesc &desc)
    {
        const ActorHandle handle = world.CreateActor();
        Actor *const actor = world.FindActor(handle);
        PointLightComponent *const light =
            actor != nullptr ? actor->AddComponent<PointLightComponent>() : nullptr;
        if (light == nullptr || !actor->SetRootComponent(light))
        {
            (void)world.DestroyActor(handle);
            return {};
        }

        light->SetLocalLocation(desc.position);
        light->SetColor(desc.color);
        light->SetIntensity(desc.intensity);
        light->SetRange(desc.range);
        light->SetLightEnabled(desc.enabled);
        light->SetCastsShadow(desc.casts_shadow);
        if (!world.InitializeActor(handle) || !world.ActivateActor(handle))
        {
            (void)world.DestroyActor(handle);
            return {};
        }
        return handle;
    }
}
