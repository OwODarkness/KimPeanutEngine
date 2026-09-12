#include "window_system.h"
#include "glfw_window_system.h"
namespace kpengine
{
    std::unique_ptr<WindowSystem> WindowSystem::CreateWindowSystem(WindowAPIType window_api_type)
    {
        switch (window_api_type)
        {
        case WindowAPIType::WINDOW_API_GLFW:
            return std::make_unique<GLFW_WindowSystem>();
            break;
        default:
            return nullptr;
            break;
        }
        return nullptr;
    }



    void WindowSystem::SetWindowSize(int width, int height)
    {
        width_ = width;
        height_ = height;
    }

    void WindowSystem::RequestWindowSize(int width, int height)
    {
        SetWindowSize(width, height);
        ResizeEvent event{};
        event.width = width;
        event.height = height;
        resize_event_dispatcher_.Dispatch(event);
    }

    void WindowSystem::QueueWindowSizeRequest(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queued_window_size_ = std::make_pair(width, height);
    }

    bool WindowSystem::ApplyQueuedWindowSizeRequest()
    {
        std::optional<std::pair<int, int>> request;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            request = queued_window_size_;
            queued_window_size_.reset();
        }
        if (!request.has_value())
        {
            return false;
        }
        RequestWindowSize(request->first, request->second);
        return true;
    }

    void WindowSystem::GetRecordedWindowSize(int &width, int &height) const noexcept
    {
        width = width_;
        height = height_;
    }

}