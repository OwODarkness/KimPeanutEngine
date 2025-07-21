#ifndef KPENGINE_RUNTIME_INPUT_ACTION_H
#define KPENGINE_RUNTIME_INPUT_ACTION_H

#include <string>
#include <functional>
#include <variant>

#include "runtime/core/math/math_header.h"

namespace kpengine::input{

enum class InputTriggleType{
    Pressed,
    Released,
    Held
};

enum class InputValueType{
    Bool,
    Axis1D,
    Axis2D,
    Axis3D
};

class InputAction{

public:
    std::string name_;
    InputTriggleType triggle_type_;
    InputValueType value_type_;
    std::variant<bool, float, Vector2f, Vector3f> value_;
    std::function<void (const InputAction&)> action;
};

}

#endif