#ifndef KPENGINE_EDITOR_WINDOW_SYSTEM_H
#define KPENGINE_EDITOR_WINDOW_SYSTEM_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace kpengine
{
    struct WindowInitInfo
    {

        static WindowInitInfo GetDefaultWindowInfo()
        {
            WindowInitInfo window_info;
            window_info.width = 1280;
            window_info.height = 720;
            return window_info;
        }

        int width;
        int height;
    };


    class WindowSystem
    {
    public:
        WindowSystem();
        virtual ~WindowSystem();

        void Initialize(WindowInitInfo window_info);

        static void OnErrorCallback(int error_code, const char* msg);

        static void OnFrameBufferSizeCallback(struct GLFWwindow*window, int width, int height);

        bool ShouldClose() const;

        void PollEvents() const;

        void Tick();

        struct GLFWwindow* GetOpenGLWndow() const{return window_;}
    private:
       struct GLFWwindow* window_;

       
    };
}

#endif