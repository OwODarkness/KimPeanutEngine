#ifndef KPENGINE_RUNTIME_INPUT_BINDING_H
#define KPENGINE_RUNTIME_INPUT_BINDING_H

#include "input_action.h"

#include <string>

namespace kpengine::input
{
    enum class InputDevice{
        Keyboard,
        Mouse,
        Gamepad
    };

    struct InputKey{
        int code;
        InputDevice device;
        
        bool operator==(const InputKey& other) const {
            return device == other.device && code == other.code;
        }
    };

    struct InputKeyHasher {
        std::size_t operator()(const InputKey& key) const {
            return std::hash<int>()(static_cast<int>(key.device)) ^ (std::hash<int>()(key.code) << 1);
        }
    };

    struct InputBinding
    {
        InputKey key;
        std::vector<int> modifiers; // ctrl, shift etc
        std::string input_action_name;
    };

    
}

#endif