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

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, &WindowSystem::OnFrameBufferSizeCallback);
        glfwSetMouseButtonCallback(window_, &WindowSystem::OnMouseButtonCallback);
        glfwSetKeyCallback(window_, &WindowSystem::OnKeyCallback);
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

    void WindowSystem::OnFrameBufferSizeCallback(GLFWwindow*window, int width, int height)
    {
        runtime::global_runtime_context.render_system_->GetRenderScene()->scene_->ReSizeFrameBuffer(width, height);
        glViewport(0, 0, width, height);
    }

    void WindowSystem::OnMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        WindowSystem* window_system = static_cast<WindowSystem*>(glfwGetWindowUserPointer(window));
        window_system->MouseButtonExec(button, action, mods);

    }

    void WindowSystem::OnKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        WindowSystem* window_system = static_cast<WindowSystem*>(glfwGetWindowUserPointer(window));
        window_system->KeyExec(key, scancode, action, mods);
    }

    void WindowSystem::RegisterOnMouseButtionFunc(MouseButtonFuncType func)
    {
        button_func_group_.push_back(func);
    }

    void WindowSystem::RegisterOnKeyFunc(KeyFuncType func)
    {
        key_func_group_.push_back(func);
    }

    void WindowSystem::MouseButtonExec(int code, int action, int mods)
    {
        for(auto& func: button_func_group_)
        {
            func(code, action, mods);
        }
    }

    void WindowSystem::KeyExec(int key, int code, int action,int mods)
    {
        for(auto& func : key_func_group_)
        {
            func(key, code, action, mods);
        }
    }
}