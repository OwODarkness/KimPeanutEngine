#ifndef KPENGINE_RUNTIME_COMMON_TYPE_H
#define KPENGINE_RUNTIME_COMMON_TYPE_H

namespace kpengine
{
    enum class GraphicsAPIType
    {
        GRAPHICS_API_OPENGL,
        GRAPHICS_API_VULKAN
    };

    enum class WindowAPIType
    {
        WINDOW_API_GLFW,
        WINDOW_API_SDL,
        WINDOW_API_Win32
    };
}

#endif