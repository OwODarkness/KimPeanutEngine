#include "gameplay/factory/spot_light_actor_factory.h"

#include "gameplay/actor/actor.h"
#include "gameplay/component/spot_light_component.h"
#include "gameplay/scene_transform_utils.h"
#include "gameplay/world/gameplay_world.h"

namespace kpengine::gameplay
{
    ActorHandle CreateSpotLightActor(GameplayWorld &world, const SpotLightActorDesc &desc)
    {
        Rotatorf rotation;
        if (!TryMakeSceneForwardRotation(desc.direction, rotation))
        {
            return {};
        }

        const ActorHandle handle = world.CreateActor();
        Actor *const actor = world.FindActor(handle);
        SpotLightComponent *const light =
            actor != nullptr ? actor->AddComponent<SpotLightComponent>() : nullptr;
        if (light == nullptr || !actor->SetRootComponent(light))
        {
            (void)world.DestroyActor(handle);
            return {};
        }

        light->SetLocalLocation(desc.position);
        light->SetLocalRotation(rotation);
        light->SetColor(desc.color);
        light->SetIntensity(desc.intensity);
        light->SetRange(desc.range);
        light->SetInnerConeRadians(desc.inner_cone_radians);
        light->SetOuterConeRadians(desc.outer_cone_radians);
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
