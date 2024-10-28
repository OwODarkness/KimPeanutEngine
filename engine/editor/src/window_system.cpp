#include "window_system.h"
#include "runtime/core/log/logger.h"

namespace kpengine
{
    WindowSystem::WindowSystem()
    {
    }

    WindowSystem::~WindowSystem()
    {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void WindowSystem::Initialize(WindowInitInfo window_info)
    {
        glfwSetErrorCallback(&WindowSystem::OnErrorCallback);

        if(GLFW_FALSE == glfwInit())
        {
            KP_LOG("GLFW Init", LOG_LEVEL_ERROR, "Failed to initialize glfw");
            return ;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        
        window_ = glfwCreateWindow(window_info.width, window_info.height, "KimPeanut Engine", nullptr, nullptr);
        if(window_ = nullptr)
        {
            KP_LOG("GLFW Window Create", LOG_LEVEL_ERROR, "Failed to create winodw");
        }

            glfwMakeContextCurrent(window_);
            glfwSwapInterval(1);
            gladLoadGL();
    }

    void WindowSystem::PollEvents() const
    {
        glfwPollEvents();
    }

    bool WindowSystem::ShouldClose() const
    {
        return glfwWindowShouldClose(window_);
    }

    void WindowSystem::OnErrorCallback(int error_code, const char* msg)
    {
        fprintf(stderr, "GLFW Error %d: %s\n", error_code, msg);
    }

}