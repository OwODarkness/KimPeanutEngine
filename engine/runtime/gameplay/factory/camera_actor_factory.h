#ifndef KPENGINE_RUNTIME_GAMEPLAY_FACTORY_CAMERA_ACTOR_FACTORY_H
#define KPENGINE_RUNTIME_GAMEPLAY_FACTORY_CAMERA_ACTOR_FACTORY_H

#include "gameplay/actor/actor_types.h"
#include "math/math_header.h"
#include "render/camera_source.h"

namespace kpengine::gameplay
{
    class GameplayWorld;

    struct CameraActorDesc
    {
        Transform3f transform{{0.0f, 0.0f, 300.0f}, {0.0f, -90.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        float field_of_view_degrees = 45.0f;
        float near_plane = 1.0f;
        float far_plane = 2000.0f;
        float orthographic_height = 10.0f;
        render::CameraProjectionMode projection_mode = render::CameraProjectionMode::Perspective;
        bool enabled = true;
        int priority = 0;
    };

    // Creates a neutral root-camera composition. Player control is layered on
    // later by PlayerController::Possess rather than being part of the factory.
    ActorHandle CreateCameraActor(GameplayWorld &world, const CameraActorDesc &desc = {});
}

#endif
