#include "gameplay/factory/directional_light_actor_factory.h"

#include "gameplay/actor/actor.h"
#include "gameplay/component/directional_light_component.h"
#include "gameplay/scene_transform_utils.h"
#include "gameplay/world/gameplay_world.h"

namespace kpengine::gameplay
{
    ActorHandle CreateDirectionalLightActor(GameplayWorld &world,
                                            const DirectionalLightActorDesc &desc)
    {
        Rotatorf rotation;
        if (!TryMakeSceneForwardRotation(desc.direction, rotation))
        {
            return {};
        }

        const ActorHandle handle = world.CreateActor();
        Actor *const actor = world.FindActor(handle);
        DirectionalLightComponent *const light =
            actor != nullptr ? actor->AddComponent<DirectionalLightComponent>() : nullptr;
        if (light == nullptr || !actor->SetRootComponent(light))
        {
            (void)world.DestroyActor(handle);
            return {};
        }

        light->SetLocalRotation(rotation);
        light->SetColor(desc.color);
        light->SetIntensity(desc.intensity);
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
