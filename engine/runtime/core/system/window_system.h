#ifndef KPENGINE_EDITOR_WINDOW_SYSTEM_H
#define KPENGINE_EDITOR_WINDOW_SYSTEM_H

#include <functional>
#include <vector>

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

        static void OnMouseButtonCallback(struct GLFWwindow* window, int button, int action, int mods);

        static void OnFrameBufferSizeCallback(struct GLFWwindow*window, int width, int height);

        static void OnKeyCallback(struct GLFWwindow* window, int key, int scancode, int action, int mods);

        bool ShouldClose() const;

        void PollEvents() const;

        void Tick(float DeltaTime);

        struct GLFWwindow* GetOpenGLWndow() const{return window_;}

        using MouseButtonFuncType = std::function<void(int code, int action, int mods)>;
        using KeyFuncType = std::function<void(int key, int code, int action, int mods)>;


        void RegisterOnMouseButtionFunc(MouseButtonFuncType func);
        void RegisterOnKeyFunc(KeyFuncType func);

        void MouseButtonExec(int code, int action, int mods);
        void KeyExec(int key, int code, int action, int mods);

    private:
       struct GLFWwindow* window_;

        std::vector<MouseButtonFuncType> button_func_group_;
        std::vector<KeyFuncType> key_func_group_;
    };
}

#endif