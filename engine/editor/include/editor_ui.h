#ifndef KPENGINE_EDITOR_EDITOR_UI_H
#define KPENGINE_EDITOR_EDITOR_UI_H

#include <vector>
#include <memory>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>


namespace kpengine{
    class Mesh;
    class ShaderHelper;
    class FrameBuffer;
namespace ui{
    
    class EditorUIComponent;



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
        std::vector<EditorUIComponent*> ui_components_;
        EditorUIComponent* main_menubar_;

        Mesh* mesh;
        ShaderHelper* shader;
        std::shared_ptr<FrameBuffer> frame_buffer;  
    };
    
}
}

#endif