#ifndef KPENGINE_RUNTIME_WINDOW_SYSTEM_H
#define KPENGINE_RUNTIME_WINDOW_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
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

        // Records a size the window already has. Platform callbacks use this;
        // application code that wants the window to change size calls
        // RequestWindowSize instead.
        void SetWindowSize(int width, int height);

        // Asks the platform to resize the client area. A window system backed by
        // a real window reports the applied size through
        // resize_event_dispatcher_ once the platform grants it, because a
        // backend that recreates its surface from what the window actually has
        // (Vulkan) cannot use a size that was only recorded locally. This base
        // implementation has no window to ask, so it records and dispatches
        // directly.
        virtual void RequestWindowSize(int width, int height);

        // Requests a resize from any thread, including the game thread that
        // serves Runtime commands. A real window may only be touched on the
        // window/render thread, so the request is recorded here and applied by
        // ApplyQueuedWindowSizeRequest on that thread. A newer request replaces
        // an unapplied older one; the last size asked for is the one that lands.
        void QueueWindowSizeRequest(int width, int height);

        // Applies a queued request on the window/render thread. Returns true
        // when a request was applied, false when none was pending.
        bool ApplyQueuedWindowSizeRequest();

        // Reports the size the window most recently recorded, which is the size
        // the platform granted rather than a request that has not landed yet.
        // Written by the window thread, so a caller on another thread sees the
        // value from some earlier frame; it is a progress report, not a barrier.
        void GetRecordedWindowSize(int &width, int &height) const noexcept;

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
        // Zero until the platform reports a size. GetRecordedWindowSize is
        // callable before Initialize, so these must not be indeterminate.
        int width_ = 0;
        int height_ = 0;

    private:
        // Guards queued_window_size_ only. width_/height_ stay thread-confined to
        // the window thread, which is why the accessor above documents itself as
        // a progress report rather than a synchronized read.
        mutable std::mutex queue_mutex_;
        std::optional<std::pair<int, int>> queued_window_size_;
    };
}

#endif
