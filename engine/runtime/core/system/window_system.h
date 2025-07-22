#ifndef KPENGINE_RUNTIME_WINDOW_SYSTEM_H
#define KPENGINE_RUNTIME_WINDOW_SYSTEM_H

#include <functional>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

struct GLFWwinodw;

namespace kpengine
{
    struct WindowInitInfo
    {

        static WindowInitInfo GetDefaultWindowInfo()
        {
            WindowInitInfo window_info;
            window_info.width = 1920;
            window_info.height = 1080;
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

        //makecontext should bind into specify thread
        void MakeContext();

        void ClearContext();

        bool ShouldClose() const;

        void PollEvents() const;

        void Tick(float DeltaTime);

        struct GLFWwindow* GetOpenGLWndow() const{return window_;}



        void MouseButtonExec(int code, int action, int mods);
        void KeyExec(int key, int code, int action, int mods);
        void CursorPosExec(double xpos, double ypos);

    private:
    static void OnErrorCallback(int error_code, const char* msg);
    static void OnFrameBufferSizeCallback(GLFWwindow*window, int width, int height);

    public:
        int width_;
        int height_;

    private:
       struct GLFWwindow* window_;

    };
}

#endif