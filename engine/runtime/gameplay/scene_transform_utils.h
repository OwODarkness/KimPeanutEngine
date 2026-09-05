#ifndef KPENGINE_RUNTIME_GAMEPLAY_SCENE_TRANSFORM_UTILS_H
#define KPENGINE_RUNTIME_GAMEPLAY_SCENE_TRANSFORM_UTILS_H

#include "math/math_header.h"

namespace kpengine::gameplay
{
    // Gameplay scene forward is +X. The pitch/yaw convention matches the
    // CameraComponent basis: zero rotation points along +X.
    Vector3f GetSceneForwardDirection(const Rotatorf &rotation);

    // Converts a non-zero finite direction into the canonical +X-forward
    // pitch/yaw representation. Roll is intentionally zero.
    bool TryMakeSceneForwardRotation(const Vector3f &direction, Rotatorf &rotation);
}

#endif
