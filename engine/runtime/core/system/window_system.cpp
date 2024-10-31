#include "window_system.h"
#include "runtime/core/log/logger.h"
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/scene_system.h"
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

        window_ = glfwCreateWindow(window_info.width, window_info.height, "KimPeanut Engine", nullptr, nullptr);
        if (window_ == nullptr)
        {
            KP_LOG("GLFW Window Create", LOG_LEVEL_ERROR, "Failed to create winodw");
            return;
        }

        glfwMakeContextCurrent(window_);
        // glfwSwapInterval(1);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            return;
        }

        glfwSetFramebufferSizeCallback(window_, &WindowSystem::OnFrameBufferSizeCallback);

    }

    void WindowSystem::PollEvents() const
    {
        glfwPollEvents();
    }

    void WindowSystem::Update()
    {

        glfwSwapBuffers(window_);
        PollEvents();

        glClearColor(0.1f, 0.1f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

    }

    bool WindowSystem::ShouldClose() const
    {
        return glfwWindowShouldClose(window_);
    }

    void WindowSystem::OnErrorCallback(int error_code, const char *msg)
    {
        fprintf(stderr, "GLFW Error %d: %s\n", error_code, msg);
    }

    void WindowSystem::OnFrameBufferSizeCallback(struct GLFWwindow*window, int width, int height)
    {
        runtime::global_runtime_context.scene_system_->scene_->ReSizeFrameBuffer(width, height);
        glViewport(0, 0, width, height);
    }
}