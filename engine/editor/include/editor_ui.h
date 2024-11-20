#ifndef KPENGINE_EDITOR_UI_H
#define KPENGINE_EDITOR_UI_H


#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>


namespace kpengine{

namespace ui{
    
    class EditorUIComponent;
    class EditorWindowComponent;

    class EditorUI{
    public:
        EditorUI();
        virtual ~EditorUI();

        void Initialize(GLFWwindow* window);
        bool Render();
        void Close();
        void BeginDraw();
        void EndDraw();
        void MainMenuBarRender();
    private:

    private:
        EditorUIComponent* main_menubar_;
        EditorWindowComponent* window_component_;
        int x = 0;
    };
    
}
}

#endif