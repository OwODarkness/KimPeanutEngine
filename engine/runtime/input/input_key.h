#ifndef KPENGINE_RUNTIME_INPUT_KEY_H
#define KPENGINE_RUNTIME_INPUT_KEY_H

#include <string>

#define KPENGINE_MOUSE_CURSOR 100

namespace kpengine::input
{
    enum class InputDevice{
        Keyboard,
        Mouse,
        Gamepad
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