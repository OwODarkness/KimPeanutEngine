#ifndef KPENGINE_RUNTIME_COMMON_EVENT_H
#define KPENGINE_RUNTIME_COMMON_EVENT_H

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

    struct ResizeEvent{
        int width;
        int height;
    };
}

#endif