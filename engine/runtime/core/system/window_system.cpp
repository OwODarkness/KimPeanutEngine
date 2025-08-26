#include "window_system.h"
#include "log/logger.h"
#include "runtime_global_context.h"
namespace kpengine
{
    WindowSystem::WindowSystem()
    {
    }

    WindowSystem::~WindowSystem()
    {
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void WindowSystem::Initialize(WindowInitInfo window_info)
    {
        glfwSetErrorCallback(&WindowSystem::OnErrorCallback);

        if (GLFW_FALSE == glfwInit())
        {
            KP_LOG("WindowSystemLog", LOG_LEVEL_ERROR, "Failed to initialize glfw");
            throw std::runtime_error("Failed to initialize glfw");
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4);

        width_ = window_info.width;
        height_ = window_info.height;
        window_ = glfwCreateWindow(window_info.width, window_info.height, "KimPeanut Engine", nullptr, nullptr);
        if (window_ == nullptr)
        {
            KP_LOG("WindowSystemLog", LOG_LEVEL_ERROR, "Failed to create winodw");
            throw std::runtime_error("Failed to create winodw");
        }
    }

    void WindowSystem::MakeContext()
    {
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);

        glfwSetFramebufferSizeCallback(window_, &WindowSystem::OnFrameBufferSizeCallback);
    }


    void WindowSystem::PollEvents() const
    {
        glfwPollEvents();
    }

    void WindowSystem::Tick(float DeltaTime)
    {
        glfwSwapBuffers(window_);
        PollEvents();
    }

    bool WindowSystem::ShouldClose() const
    {
        return glfwWindowShouldClose(window_);
    }

    void WindowSystem::OnErrorCallback(int error_code, const char *msg)
    {
        fprintf(stderr, "GLFW Error %d: %s\n", error_code, msg);
    }

    void WindowSystem::OnFrameBufferSizeCallback(GLFWwindow *window, int width, int height)
    {
        GLFWAppContext *glfw_context = static_cast<GLFWAppContext *>(glfwGetWindowUserPointer(window));
        if (!glfw_context || !glfw_context->window_sys)
            return;

        WindowSystem *window_system = glfw_context->window_sys;
        window_system->width_ = width;
        window_system->height_ = height;
    }

}