#include "glfw_window_system.h"

#include <GLFW/glfw3.h>
#include <stb_image/image_helper.h>
#include "config/path.h"
#include "log/logger.h"
namespace kpengine
{

    bool GLFW_WindowSystem::Initialize(const WindowCreateInfo &create_info)
    {
        width_ = create_info.width;
        height_ = create_info.height;
        title_ = create_info.title;

        glfwSetErrorCallback(&GLFW_WindowSystem::OnErrorCallback);

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
            should_make_context_ = true;
            glfwSwapInterval(1); // vsync
        }

        glfwSetFramebufferSizeCallback(window_, GLFW_WindowSystem::OnFrameBufferSizeCallback);
        glfwSetMouseButtonCallback(window_, GLFW_WindowSystem::OnMouseButtonCallback);
        glfwSetKeyCallback(window_, GLFW_WindowSystem::OnKeyCallback);
        glfwSetCursorPosCallback(window_, GLFW_WindowSystem::OnCursorPosCallback);
        glfwSetScrollCallback(window_, GLFW_WindowSystem::OnScrollCallback);

        // Window icon from config/icon.png. glfwSetWindowIcon copies the pixels, so
        // the decode buffer is freed immediately. Non-fatal: window still works bare.
        {
            int w = 0, h = 0, ch = 0;
            stbi_uc *pixels = stbi_load(GetIconPath().c_str(), &w, &h, &ch, STBI_rgb_alpha);
            if (pixels)
            {
                GLFWimage image{};
                image.width = static_cast<unsigned int>(w);
                image.height = static_cast<unsigned int>(h);
                image.pixels = pixels;
                glfwSetWindowIcon(window_, 1, &image);
                stbi_image_free(pixels);
                KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_INFO, "Window icon set from %s (%dx%d)", GetIconPath().c_str(), w, h);
            }
            else
            {
                KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_WARNING, "Failed to load window icon from %s", GetIconPath().c_str());
            }
        }

        return true;
    }
    void GLFW_WindowSystem::PollEvents()
    {
        glfwPollEvents();
    }
    void GLFW_WindowSystem::SwapBuffers()
    {
        glfwSwapBuffers(window_);
    }

    void GLFW_WindowSystem::Tick(float delta_time)
    {
        PollEvents();
        SwapBuffers();
    }

    void GLFW_WindowSystem::Cleanup()
    {
        if (should_make_context_)
        {
            glfwMakeContextCurrent(nullptr);
        }
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    WindowHandle GLFW_WindowSystem::GetNativeHandle() const
    {
        return static_cast<WindowHandle>(window_);
    }

    bool GLFW_WindowSystem::ShouldClose() const
    {
        return glfwWindowShouldClose(window_);
    }

    GLFW_WindowSystem::~GLFW_WindowSystem()
    {
    }

    void GLFW_WindowSystem::OnErrorCallback(int error_code, const char *msg)
    {
        KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, msg);
    }
    void GLFW_WindowSystem::OnFrameBufferSizeCallback(GLFWwindow *window, int width, int height)
    {
        WindowSystem *window_sys = static_cast<WindowSystem *>(glfwGetWindowUserPointer(window));
        if (!window_sys)
        {
            KP_LOG("GLFWWindowSystemLog", LOG_LEVEL_ERROR, "Failed to cast window_user_pointer to WindowSystem*");
            throw std::runtime_error("Failed to cast window_user_pointer to WindowSystem*");
        }
        window_sys->SetWindowSize(width, height);
        ResizeEvent event{};
        event.width = width;
        event.height = height;
        window_sys->resize_event_dispatcher_.Dispatch(event);
    }

    void GLFW_WindowSystem::OnMouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
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
    void GLFW_WindowSystem::OnKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
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
    void GLFW_WindowSystem::OnCursorPosCallback(GLFWwindow *window, double xpos, double ypos)
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
    void GLFW_WindowSystem::OnScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
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
