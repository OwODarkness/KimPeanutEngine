#ifndef KPENGINE_RUNTIME_GLFW_WINDOW_SYSTEM_H
#define KPENGINE_RUNTIME_GLFW_WINDOW_SYSTEM_H


#include "window_system.h"

struct GLFWwindow;
namespace kpengine
{
    class GLFWWindowSystem : public WindowSystem
    {
    public:
        bool Initialize(const WindowCreateInfo &create_info) override;
        void PollEvents() override;
        void SwapBuffers() override;
        WindowHandle GetNativeHandle() const override;
        bool ShouldClose() const override;
        void Tick(float delta_time) override;
        ~GLFWWindowSystem();

    private:
        static void OnErrorCallback(int error_code, const char *msg);
        static void OnFrameBufferSizeCallback(GLFWwindow *window, int width, int height);
        
        static void OnMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void OnKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void OnCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void OnScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    
    private:
        GLFWwindow* window_;
    };
}

#endif