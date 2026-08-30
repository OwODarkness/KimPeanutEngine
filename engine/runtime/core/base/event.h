#ifndef KPENGINE_RUNTIME_COMMON_EVENT_H
#define KPENGINE_RUNTIME_COMMON_EVENT_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace kpengine{
    struct MouseButtonEvent{
        int code;
        int action;
        int mods;
    };
        struct  KeyEvent{
            int key;
            int code;
            int action;
            int mods;
    };
    struct CursorEvent{
        double xpos;
        double ypos;
    };
    struct ScrollEvent{
        double xoffset;
        double yoffset;
    };

    constexpr std::size_t kGamepadAxisCount = 6;
    constexpr std::size_t kGamepadButtonCount = 15;

    // Window backends translate their native controller state into this copied
    // sample. Consumers must not retain pointers into backend state.
    struct GamepadStateEvent
    {
        int gamepad_index = -1;
        bool connected = false;
        std::array<float, kGamepadAxisCount> axes{};
        std::array<uint8_t, kGamepadButtonCount> buttons{};
    };

    struct ResizeEvent{
        int width;
        int height;
    };
}

#endif
