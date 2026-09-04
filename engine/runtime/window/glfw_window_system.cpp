#include "glfw_window_system.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#define GLFW_INCLUDE_NONE
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <glad/glad.h>
#include <stb_image/image_helper.h>
#include "config/path.h"
#include "log/logger.h"
namespace kpengine
{
    GLFW_WindowSystem::~GLFW_WindowSystem()
    {
    }


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

        // Poll normalized controller state once per render frame. Only copied
        // values cross the Window -> Input boundary; Gameplay never sees GLFW.
        for (int gamepad_index = GLFW_JOYSTICK_1; gamepad_index <= GLFW_JOYSTICK_LAST;
             ++gamepad_index)
        {
            GamepadStateEvent event{};
            event.gamepad_index = gamepad_index;
            GLFWgamepadstate state{};
            if (glfwGetGamepadState(gamepad_index, &state) == GLFW_TRUE)
            {
                event.connected = true;
                for (std::size_t axis = 0; axis < event.axes.size(); ++axis)
                {
                    event.axes[axis] = state.axes[axis];
                }
                for (std::size_t button = 0; button < event.buttons.size(); ++button)
                {
                    event.buttons[button] = state.buttons[button];
                }
            }
            gamepad_event_dispatcher_.Dispatch(event);
        }
    }
    void GLFW_WindowSystem::SwapBuffers()
    {
        glfwSwapBuffers(window_);
    }

    WindowCaptureResult GLFW_WindowSystem::CaptureWindow()
    {
        WindowCaptureResult result{};
#if !defined(_WIN32)
        result.diagnostic = "Engine window capture is only implemented on Windows";
        return result;
#else
        if (window_ == nullptr)
        {
            result.diagnostic = "Cannot capture an uninitialized GLFW window";
            return result;
        }

        if (should_make_context_)
        {
            GLint viewport[4]{};
            glGetIntegerv(GL_VIEWPORT, viewport);
            if (viewport[2] > 0 && viewport[3] > 0)
            {
                const size_t pixel_count = static_cast<size_t>(viewport[2]) *
                                           static_cast<size_t>(viewport[3]);
                result.width = static_cast<uint32_t>(viewport[2]);
                result.height = static_cast<uint32_t>(viewport[3]);
                result.rgba8_pixels.resize(pixel_count * 4);

                GLint previous_pack_alignment = 4;
                glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3],
                             GL_RGBA, GL_UNSIGNED_BYTE, result.rgba8_pixels.data());
                glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
                if (glGetError() == GL_NO_ERROR)
                {
                    // OpenGL's origin is bottom-left while PNG's is top-left.
                    const size_t row_byte_count = static_cast<size_t>(viewport[2]) * 4;
                    for (int row = 0; row < viewport[3] / 2; ++row)
                    {
                        auto *top = result.rgba8_pixels.data() +
                                    static_cast<size_t>(row) * row_byte_count;
                        auto *bottom = result.rgba8_pixels.data() +
                                       static_cast<size_t>(viewport[3] - row - 1) *
                                           row_byte_count;
                        for (size_t byte = 0; byte < row_byte_count; ++byte)
                        {
                            std::swap(top[byte], bottom[byte]);
                        }
                    }
                    return result;
                }
                result.width = 0;
                result.height = 0;
                result.rgba8_pixels.clear();
            }
        }

        HWND native_window = glfwGetWin32Window(window_);
        RECT client_rect{};
        if (native_window == nullptr || !GetClientRect(native_window, &client_rect))
        {
            result.diagnostic = "Could not query the engine window client rectangle";
            return result;
        }

        const LONG width = client_rect.right - client_rect.left;
        const LONG height = client_rect.bottom - client_rect.top;
        if (width <= 0 || height <= 0 ||
            static_cast<uint64_t>(width) > (std::numeric_limits<uint32_t>::max)() ||
            static_cast<uint64_t>(height) > (std::numeric_limits<uint32_t>::max)())
        {
            result.diagnostic = "Engine window client rectangle is empty or too large";
            return result;
        }

        HDC window_dc = GetDC(native_window);
        HDC capture_dc = window_dc != nullptr ? CreateCompatibleDC(window_dc) : nullptr;
        void *pixels = nullptr;
        HBITMAP bitmap = nullptr;
        if (window_dc != nullptr && capture_dc != nullptr)
        {
            BITMAPINFO bitmap_info{};
            bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap_info.bmiHeader.biWidth = width;
            // A negative height makes the DIB top-down, matching the PNG row order.
            bitmap_info.bmiHeader.biHeight = -height;
            bitmap_info.bmiHeader.biPlanes = 1;
            bitmap_info.bmiHeader.biBitCount = 32;
            bitmap_info.bmiHeader.biCompression = BI_RGB;
            bitmap = CreateDIBSection(window_dc, &bitmap_info, DIB_RGB_COLORS, &pixels,
                                      nullptr, 0);
        }

        if (bitmap == nullptr || pixels == nullptr)
        {
            if (capture_dc != nullptr)
            {
                DeleteDC(capture_dc);
            }
            if (window_dc != nullptr)
            {
                ReleaseDC(native_window, window_dc);
            }
            result.diagnostic = "Could not allocate an engine window capture bitmap";
            return result;
        }

        HGDIOBJ previous_bitmap = SelectObject(capture_dc, bitmap);
        POINT client_origin{0, 0};
        HDC screen_dc = GetDC(nullptr);
        bool captured = false;
        if (screen_dc != nullptr && ClientToScreen(native_window, &client_origin))
        {
            // OpenGL/Vulkan content is already composited into the visible
            // client area after present; BitBlt preserves that final image.
            captured = BitBlt(capture_dc, 0, 0, width, height, screen_dc,
                              client_origin.x, client_origin.y, SRCCOPY | CAPTUREBLT) != FALSE;
        }
        if (screen_dc != nullptr)
        {
            ReleaseDC(nullptr, screen_dc);
        }
        if (!captured)
        {
            // This path can still produce a useful image for a covered window
            // when the native window implementation supports WM_PRINT.
            const UINT print_flags = PW_CLIENTONLY | PW_RENDERFULLCONTENT;
            captured = PrintWindow(native_window, capture_dc, print_flags) != FALSE;
        }

        if (captured)
        {
            const size_t pixel_count = static_cast<size_t>(width) *
                                       static_cast<size_t>(height);
            result.width = static_cast<uint32_t>(width);
            result.height = static_cast<uint32_t>(height);
            result.rgba8_pixels.resize(pixel_count * 4);
            const auto *source = static_cast<const uint8_t *>(pixels);
            for (size_t index = 0; index < pixel_count; ++index)
            {
                result.rgba8_pixels[index * 4 + 0] = source[index * 4 + 2];
                result.rgba8_pixels[index * 4 + 1] = source[index * 4 + 1];
                result.rgba8_pixels[index * 4 + 2] = source[index * 4 + 0];
                result.rgba8_pixels[index * 4 + 3] = 255;
            }
        }
        else
        {
            result.diagnostic = "Windows could not capture the engine window client area";
        }

        SelectObject(capture_dc, previous_bitmap);
        DeleteObject(bitmap);
        DeleteDC(capture_dc);
        ReleaseDC(native_window, window_dc);
        return result;
#endif
    }

    void GLFW_WindowSystem::SetMouseCapture(bool captured)
    {
        if (window_ == nullptr || mouse_captured_ == captured)
        {
            return;
        }

        glfwSetInputMode(window_, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported())
        {
            glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
        }
        mouse_captured_ = captured;
    }

    bool GLFW_WindowSystem::IsMouseCaptured() const
    {
        return mouse_captured_;
    }


    void GLFW_WindowSystem::Cleanup()
    {
        if (window_ != nullptr)
        {
            SetMouseCapture(false);
        }
        if (should_make_context_)
        {
            glfwMakeContextCurrent(nullptr);
        }
        glfwDestroyWindow(window_);
        window_ = nullptr;
        should_make_context_ = false;
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
