#ifndef KPENGINE_RUNTIME_INPUT_ACTION_H
#define KPENGINE_RUNTIME_INPUT_ACTION_H

#include <string>
#include <functional>
#include <variant>

#include "math/math_header.h"

namespace kpengine::input
{

    enum class InputTriggleType
    {
        Pressed,
        Released,
        Held
    };

    enum class InputValueType
    {
        Bool,
        Axis1D,
        Axis2D,
        Axis3D
    };

    struct InputState
    {
        InputTriggleType triggle_type;
        std::variant<bool, float, Vector2f, Vector3f> value;
    };

    class InputAction
    {
    public:
        std::string name_;
        InputValueType value_type_;
        std::variant<bool, float, Vector2f, Vector3f> default_value;
        std::function<void(const InputState &)> callback_;
    };

}

#endif