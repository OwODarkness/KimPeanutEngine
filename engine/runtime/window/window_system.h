#ifndef KPENGINE_RUNTIME_WINDOW_SYSTEM_H
#define KPENGINE_RUNTIME_WINDOW_SYSTEM_H

#include <memory>
#include <string>
#include "common/common.h"
#include "delegate/event_dispatcher.h"
namespace kpengine
{

    using WindowHandle = void *;

    struct WindowCreateInfo
    {
        int width = 1920;
        int height = 1080;
        std::string title;
        GraphicsAPIType graphics_api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
    };

    class WindowSystem
    {
    public:
        virtual ~WindowSystem() = default;
        virtual bool Initialize(const WindowCreateInfo &create_info) = 0;
        virtual void PollEvents() = 0;
        virtual void SwapBuffers() = 0;
        virtual WindowHandle GetNativeHandle() const = 0; 
        virtual bool ShouldClose() const = 0;

        void SetWindowSize(int width, int height);
        void Tick(float delta_time);
        static std::unique_ptr<WindowSystem> CreateWindow(WindowAPIType window_api_type);

    public:
        EventDispatcher<MouseButtonEvent> mouse_button_event_dispatcher_;
        EventDispatcher<KeyEvent> key_event_dispatcher_;
        EventDispatcher<CursorEvent> cursor_event_dispatcher_;
        EventDispatcher<ScrollEvent> scroll_event_dispatcher_;

    protected:
        std::string title_;
        int width_;
        int height_;
    };
}

#endif