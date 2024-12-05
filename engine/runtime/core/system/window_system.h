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

        static void OnErrorCallback(int error_code, const char* msg);

        static void OnMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

        static void OnFrameBufferSizeCallback(GLFWwindow*window, int width, int height);

        static void OnKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

        static void OnCursorPosCallback(GLFWwindow* window, double xpos, double ypos);

        bool ShouldClose() const;

        void PollEvents() const;

        void Tick(float DeltaTime);

        struct GLFWwindow* GetOpenGLWndow() const{return window_;}

        using MouseButtonFuncType = std::function<void(int , int , int)>;
        using KeyFuncType = std::function<void(int, int, int, int)>;
        using CursorPosFuncType = std::function<void(double, double)>;

        void RegisterOnMouseButtionFunc(MouseButtonFuncType func);
        void RegisterOnKeyFunc(KeyFuncType func);
        void RegisterOnCursorPosFunc(CursorPosFuncType func);

        void MouseButtonExec(int code, int action, int mods);
        void KeyExec(int key, int code, int action, int mods);
        void CursorPosExec(double xpos, double ypos);

    public:
        int width_;
        int height_;

    private:
       struct GLFWwindow* window_;

        std::vector<MouseButtonFuncType> button_func_group_;
        std::vector<KeyFuncType> key_func_group_;
        std::vector<CursorPosFuncType> cursor_pos_func_group_;

    };
}

#endif