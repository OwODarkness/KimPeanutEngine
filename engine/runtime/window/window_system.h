#ifndef KPENGINE_RUNTIME_WINDOW_SYSTEM_H
#define KPENGINE_RUNTIME_WINDOW_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "base/base.h"
#include "delegate/event_dispatcher.h"
namespace kpengine
{


    struct WindowCreateInfo
    {
        int width = 1920;
        int height = 1080;
        std::string title;
        GraphicsAPIType graphics_api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
    };

    struct WindowCaptureResult
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba8_pixels;
        std::string diagnostic;

        bool IsSuccess() const
        {
            if (width == 0 || height == 0)
            {
                return false;
            }
            const size_t pixel_count = static_cast<size_t>(width) * height;
            return pixel_count <= (std::numeric_limits<size_t>::max)() / 4 &&
                   rgba8_pixels.size() == pixel_count * 4;
        }
    };

    class WindowSystem
    {
    public:
        virtual ~WindowSystem() = default;
        virtual bool Initialize(const WindowCreateInfo &create_info) = 0;
        virtual void PollEvents() = 0;
        virtual void SwapBuffers() = 0;
        // Captures the final client area. Must run on the window/render thread
        // at the presentation boundary so API-specific UI composition is
        // included without exposing native window types to Runtime consumers.
        virtual WindowCaptureResult CaptureWindow() = 0;
        virtual WindowHandle GetNativeHandle() const = 0; 
        virtual bool ShouldClose() const = 0;
        virtual void SetMouseCapture(bool captured) = 0;
        virtual bool IsMouseCaptured() const = 0;
        virtual void Cleanup() = 0;

        void SetWindowSize(int width, int height);
        static std::unique_ptr<WindowSystem> CreateWindowSystem(WindowAPIType window_api_type);

    public:
        EventDispatcher<MouseButtonEvent> mouse_button_event_dispatcher_;
        EventDispatcher<KeyEvent> key_event_dispatcher_;
        EventDispatcher<CursorEvent> cursor_event_dispatcher_;
        EventDispatcher<ScrollEvent> scroll_event_dispatcher_;
        EventDispatcher<GamepadStateEvent> gamepad_event_dispatcher_;
        EventDispatcher<ResizeEvent> resize_event_dispatcher_;

    protected:
        std::string title_;
        int width_;
        int height_;
    };
}

#endif
