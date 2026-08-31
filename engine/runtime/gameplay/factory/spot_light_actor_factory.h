#ifndef KPENGINE_RUNTIME_GAMEPLAY_FACTORY_SPOT_LIGHT_ACTOR_FACTORY_H
#define KPENGINE_RUNTIME_GAMEPLAY_FACTORY_SPOT_LIGHT_ACTOR_FACTORY_H

#include "gameplay/actor/actor_types.h"
#include "math/math_header.h"

namespace kpengine::gameplay
{
    class GameplayWorld;

    struct SpotLightActorDesc
    {
        Vector3f position{};
        Vector3f direction{0.0f, -1.0f, 0.0f};
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float range = 1.0f;
        float inner_cone_radians = 0.0f;
        float outer_cone_radians = 0.785398163f;
        bool enabled = true;
    };

    ActorHandle CreateSpotLightActor(GameplayWorld &world, const SpotLightActorDesc &desc);
}

#endif
