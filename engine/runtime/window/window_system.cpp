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

}