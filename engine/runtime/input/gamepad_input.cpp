#include "gamepad_input.h"

#include <algorithm>
#include <cmath>

namespace kpengine::input
{
    Vector2f ApplyGamepadStickSettings(const Vector2f &raw,
                                       const GamepadStickSettings &settings)
    {
        const float dead_zone = std::clamp(settings.dead_zone, 0.0f, 0.99f);
        const float sensitivity = std::max(settings.sensitivity, 0.0f);
        const float magnitude = raw.Norm();
        if (magnitude <= dead_zone || magnitude <= 0.0f)
        {
            return {};
        }

        const float remapped_magnitude =
            std::clamp((magnitude - dead_zone) / (1.0f - dead_zone), 0.0f, 1.0f);
        Vector2f result = raw * (remapped_magnitude / magnitude * sensitivity);
        if (settings.invert_y)
        {
            result.y_ = -result.y_;
        }
        return result;
    }
}
