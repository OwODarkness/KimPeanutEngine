#ifndef KPENGINE_RUNTIME_COMMON_TYPE_H
#define KPENGINE_RUNTIME_COMMON_TYPE_H

namespace kpengine
{
    enum class GraphicsAPIType
    {
        GRAPHICS_API_UNKNOW,
        GRAPHICS_API_OPENGL,
        GRAPHICS_API_VULKAN
    };

    enum class WindowAPIType
    {
        WINDOW_API_GLFW,
        WINDOW_API_SDL,
        WINDOW_API_Win32
    };

    // Host platform. Drives the platform seam factories (e.g. memory sampling); the
    // runtime core stays platform-agnostic and the impls live under platform/<name>/.
    enum class PlatformType
    {
        PLATFORM_WINDOWS
    };

    using WindowHandle = void *;

    struct GraphicsContext
    {
        GraphicsAPIType type;
        void *native = nullptr;
    };


}

#endif