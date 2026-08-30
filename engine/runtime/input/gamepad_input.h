#ifndef KPENGINE_RUNTIME_INPUT_GAMEPAD_INPUT_H
#define KPENGINE_RUNTIME_INPUT_GAMEPAD_INPUT_H

#include "math/math_header.h"

namespace kpengine::input
{
    struct GamepadStickSettings
    {
        float dead_zone = 0.2f;
        float sensitivity = 1.0f;
        bool invert_y = false;
    };

    struct GamepadInputSettings
    {
        GamepadStickSettings left_stick{};
        GamepadStickSettings right_stick{};
    };

    // Remaps a stick's radial magnitude from [dead_zone, 1] to [0, 1].
    // Values inside the dead zone are exactly zero; output is clamped and
    // sensitivity is applied after remapping.
    Vector2f ApplyGamepadStickSettings(const Vector2f &raw,
                                       const GamepadStickSettings &settings);
}

#endif
