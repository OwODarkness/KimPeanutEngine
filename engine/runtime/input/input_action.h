#ifndef KPENGINE_RUNTIME_INPUT_ACTION_H
#define KPENGINE_RUNTIME_INPUT_ACTION_H

#include <cstdint>
#include <string>
#include <functional>
#include <variant>
#include <vector>

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

    enum class InputActionSource
    {
        Unknown,
        Keyboard,
        Mouse,
        Gamepad,
    };

    constexpr uint8_t kInputActionComponentX = 1u << 0;
    constexpr uint8_t kInputActionComponentY = 1u << 1;
    constexpr uint8_t kInputActionComponentZ = 1u << 2;
    constexpr uint8_t kInputActionAllComponents =
        kInputActionComponentX | kInputActionComponentY | kInputActionComponentZ;

    struct InputState
    {
        InputTriggleType triggle_type;
        std::variant<bool, float, Vector2f, Vector3f> value;
    };

    struct InputActionEvent
    {
        std::string action_name;
        InputState state;
        InputActionSource source = InputActionSource::Unknown;
        uint8_t component_mask = kInputActionAllComponents;
    };

    struct InputFrameSnapshot
    {
        std::vector<InputActionEvent> events;
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
