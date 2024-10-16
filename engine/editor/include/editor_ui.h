#ifndef KPENGINE_EDITOR_EDITOR_UI_H
#define KPENGINE_EDITOR_EDITOR_UI_H

#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>


namespace kpengine{
namespace ui{
    
    class EditorUIComponent;

    struct WindowInitInfo{

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

    class EditorUI{
    public:
        EditorUI();
        virtual ~EditorUI();

        void Initialize(WindowInitInfo window_info);
        bool Render();
        void Close();

    private:
        static void GLFWErrorCallback(int error, const char* desc);

    private:
        struct GLFWwindow* window_;
        ImVec4 clear_color_;
        std::vector<EditorUIComponent*> ui_components_;
    };
    
    void RenderUI();
}
}

#endif