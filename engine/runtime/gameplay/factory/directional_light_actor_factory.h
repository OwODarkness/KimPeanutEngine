#ifndef KPENGINE_RUNTIME_GAMEPLAY_FACTORY_DIRECTIONAL_LIGHT_ACTOR_FACTORY_H
#define KPENGINE_RUNTIME_GAMEPLAY_FACTORY_DIRECTIONAL_LIGHT_ACTOR_FACTORY_H

#include "gameplay/actor/actor_types.h"
#include "math/math_header.h"

namespace kpengine::gameplay
{
    class GameplayWorld;

    struct DirectionalLightActorDesc
    {
        Vector3f direction{0.0f, -1.0f, 0.0f};
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        bool enabled = true;
    };

    // Constructs and activates the first Gameplay light Actor. Its component
    // publishes source values only; the later Render LightWorld owns resolution.
    ActorHandle CreateDirectionalLightActor(GameplayWorld &world,
                                            const DirectionalLightActorDesc &desc);
}

#endif
