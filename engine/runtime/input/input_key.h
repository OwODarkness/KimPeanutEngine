#ifndef KPENGINE_RUNTIME_INPUT_KEY_H
#define KPENGINE_RUNTIME_INPUT_KEY_H

#include <cstdint>
#include <string>

#define KPENGINE_MOUSE_CURSOR 100
#define KPENGINE_MOUSE_SCROLL 101
namespace kpengine::input
{
    enum class InputDevice{
        Keyboard,
        Mouse,
        Gamepad
    };

    // Platform-neutral keyboard codes. Window backends translate their native
    // values into this stable subset before InputContext sees them.
    enum class KeyboardKeyCode : int
    {
        A = 65,
        D = 68,
        E = 69,
        Q = 81,
        S = 83,
        W = 87,
    };

    constexpr int kMouseCursorCode = KPENGINE_MOUSE_CURSOR;
    constexpr int kMouseScrollCode = KPENGINE_MOUSE_SCROLL;

    // Logical gamepad controls. The Window layer maps native controller axes
    // and buttons to these stable codes before InputContext sees them.
    enum class GamepadAxisCode : int
    {
        LeftStick = 0,
        RightStick = 1,
        LeftTrigger = 2,
        RightTrigger = 3,
    };

    enum class GamepadButtonCode : int
    {
        A = 100,
        B = 101,
        X = 102,
        Y = 103,
        LeftBumper = 104,
        RightBumper = 105,
        Back = 106,
        Start = 107,
        Guide = 108,
        LeftThumb = 109,
        RightThumb = 110,
        DpadUp = 111,
        DpadRight = 112,
        DpadDown = 113,
        DpadLeft = 114,
    };

    struct InputKey{
        InputDevice device;
        int code;
        
        bool operator==(const InputKey& other) const {
            return device == other.device && code == other.code;
        }
    };

    struct InputKeyHasher {
        std::size_t operator()(const InputKey& key) const {
            return std::hash<int>()(static_cast<int>(key.device)) ^ (std::hash<int>()(key.code) << 1);
        }
    };

    
}

#endif
