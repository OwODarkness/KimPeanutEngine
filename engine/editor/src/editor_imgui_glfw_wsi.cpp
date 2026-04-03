#include "editor_imgui_glfw_wsi.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "GLFW/glfw3.h"

namespace kpengine::editor{
    void EditorImguiGLFWWSI::Initialize(WindowHandle handle, GraphicsAPIType type)
    {
        GLFWwindow* window = static_cast<GLFWwindow*>(handle);

        if(type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            ImGui_ImplGlfw_InitForOpenGL(window, true);
        }
        else if(type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            ImGui_ImplGlfw_InitForVulkan(window, true);
        }
    }

    void EditorImguiGLFWWSI::Shutdown()
    {
        ImGui_ImplGlfw_Shutdown();
    }

    void EditorImguiGLFWWSI::NewFrame()
    {
        ImGui_ImplGlfw_NewFrame();
    }


}