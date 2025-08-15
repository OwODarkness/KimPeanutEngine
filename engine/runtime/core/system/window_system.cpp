#include "window_system.h"
#include "runtime/core/log/logger.h"
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/render_scene.h"
#include "runtime/render/frame_buffer.h"
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

        if (GLFW_FALSE == glfwInit())
        {
            KP_LOG("GLFW Init", LOG_LEVEL_ERROR, "Failed to initialize glfw");
            return;
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
            KP_LOG("GLFW Window Create", LOG_LEVEL_ERROR, "Failed to create winodw");
            return;
        }
    }

    void WindowSystem::MakeContext()
    {

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            KP_LOG("GLFW Window Create", LOG_LEVEL_ERROR, "Failed to initialize GLAD");
            return;
        }
        glfwSetFramebufferSizeCallback(window_, &WindowSystem::OnFrameBufferSizeCallback);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
    }

    void WindowSystem::ClearContext()
    {
        glfwMakeContextCurrent(nullptr);
    }

    void WindowSystem::PollEvents() const
    {
        glfwPollEvents();
    }

    void WindowSystem::Tick(float DeltaTime)
    {
        glfwSwapBuffers(window_);
        PollEvents();

        glClearColor(0.1f, 0.1f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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
        glViewport(0, 0, width, height);
        GLFWAppContext *glfw_context = static_cast<GLFWAppContext *>(glfwGetWindowUserPointer(window));
        if (!glfw_context || !glfw_context->window_sys)
            return;

        WindowSystem *window_system = glfw_context->window_sys;
        window_system->width_ = width;
        window_system->height_ = height;
    }

}