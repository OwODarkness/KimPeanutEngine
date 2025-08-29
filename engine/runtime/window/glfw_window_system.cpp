#include "glfw_window_system.h"

#include <GLFW/glfw3.h>
#include "log/logger.h"
namespace kpengine
{

    bool GLFWWindowSystem::Initialize(const WindowCreateInfo &create_info)
    {
        width_ = create_info.width;
        height_ = create_info.height;
        title_ = create_info.title;

        glfwSetErrorCallback(&GLFWWindowSystem::OnErrorCallback);

        if (glfwInit() == GLFW_FALSE)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to initialize GLFW");
            throw std::runtime_error("Failed to initialize GLFW");
        }

        if (create_info.graphics_api_type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }
        else if (create_info.graphics_api_type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        }

        // Create window
        window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
        if (!window_)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to create GLFW window");
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwSetWindowUserPointer(window_, this);

        if (create_info.graphics_api_type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            glfwMakeContextCurrent(window_);
            glfwSwapInterval(1); // vsync
        }

        glfwSetMouseButtonCallback(window_, GLFWWindowSystem::OnMouseButtonCallback);
        glfwSetKeyCallback(window_, GLFWWindowSystem::OnKeyCallback);
        glfwSetCursorPosCallback(window_, GLFWWindowSystem::OnCursorPosCallback);
        glfwSetScrollCallback(window_, GLFWWindowSystem::OnScrollCallback);

        return true;
    }
    void GLFWWindowSystem::PollEvents()
    {
        glfwPollEvents();
    }
    void GLFWWindowSystem::SwapBuffers()
    {
        glfwSwapBuffers(window_);
    }

    void GLFWWindowSystem::Tick(float delta_time)
    {
        PollEvents();
        SwapBuffers();

    }

    WindowHandle GLFWWindowSystem::GetNativeHandle() const
    {
        return static_cast<WindowHandle>(window_);
    }

    bool GLFWWindowSystem::ShouldClose() const
    {
        return glfwWindowShouldClose(window_);
    }

    GLFWWindowSystem::~GLFWWindowSystem()
    {
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void GLFWWindowSystem::OnErrorCallback(int error_code, const char *msg)
    {
        KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, msg);
    }
    void GLFWWindowSystem::OnFrameBufferSizeCallback(GLFWwindow *window, int width, int height)
    {
        WindowSystem *window_sys = static_cast<WindowSystem *>(glfwGetWindowUserPointer(window));
        if (!window_sys)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to cast window_user_pointer to WindowSystem*");
            throw std::runtime_error("Failed to cast window_user_pointer to WindowSystem*");
        }
        window_sys->SetWindowSize(width, height);
    }

    void GLFWWindowSystem::OnMouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
    {
        WindowSystem *window_sys = static_cast<WindowSystem *>(glfwGetWindowUserPointer(window));
        if (!window_sys)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to cast window_user_pointer to WindowSystem*");
            throw std::runtime_error("Failed to cast window_user_pointer to WindowSystem*");
        }
        MouseButtonEvent event{};
        event.code = button;
        event.action = action;
        event.mods = mods;
        window_sys->mouse_button_event_dispatcher_.Dispatch(event);
    }
    void GLFWWindowSystem::OnKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
    {
        WindowSystem *window_sys = static_cast<WindowSystem *>(glfwGetWindowUserPointer(window));
        if (!window_sys)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to cast window_user_pointer to WindowSystem*");
            throw std::runtime_error("Failed to cast window_user_pointer to WindowSystem*");
        }
        KeyEvent event{};
        event.key = key;
        event.code = scancode;
        event.action = action;
        event.mods = mods;
        window_sys->key_event_dispatcher_.Dispatch(event);
    }
    void GLFWWindowSystem::OnCursorPosCallback(GLFWwindow *window, double xpos, double ypos)
    {
        WindowSystem *window_sys = static_cast<WindowSystem *>(glfwGetWindowUserPointer(window));
        if (!window_sys)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to cast window_user_pointer to WindowSystem*");
            throw std::runtime_error("Failed to cast window_user_pointer to WindowSystem*");
        }
        CursorEvent event{};
        event.xpos = xpos;
        event.ypos = ypos;
        window_sys->cursor_event_dispatcher_.Dispatch(event);
    }
    void GLFWWindowSystem::OnScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
    {
        WindowSystem *window_sys = static_cast<WindowSystem *>(glfwGetWindowUserPointer(window));
        if (!window_sys)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to cast window_user_pointer to WindowSystem*");
            throw std::runtime_error("Failed to cast window_user_pointer to WindowSystem*");
        }
        ScrollEvent event{};
        event.xoffset = xoffset;
        event.yoffset = yoffset;
        window_sys->scroll_event_dispatcher_.Dispatch(event);
    }

}
