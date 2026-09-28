#ifndef KPENGINE_RUNTIME_GAMEPLAY_FACTORY_POINT_LIGHT_ACTOR_FACTORY_H
#define KPENGINE_RUNTIME_GAMEPLAY_FACTORY_POINT_LIGHT_ACTOR_FACTORY_H

#include "gameplay/actor/actor_types.h"
#include "math/math_header.h"

namespace kpengine::gameplay
{
    class GameplayWorld;

    struct PointLightActorDesc
    {
        Vector3f position{};
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float range = 1.0f;
        bool enabled = true;
        bool casts_shadow = true;
        // Zero axes select a point; nonzero perpendicular half-axes select a rectangle.
        Vector3f half_axis_u{};
        Vector3f half_axis_v{};
    };

    ActorHandle CreatePointLightActor(GameplayWorld &world, const PointLightActorDesc &desc);
}

#endif
