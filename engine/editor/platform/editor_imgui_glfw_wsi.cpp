#include "editor/platform/editor_imgui_glfw_wsi.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <glfw/glfw3.h>

namespace kpengine::editor{
    bool EditorImguiGLFWWSI::Initialize(WindowHandle handle, GraphicsAPIType type)
    {
        GLFWwindow* window = static_cast<GLFWwindow*>(handle);

        if(type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            return ImGui_ImplGlfw_InitForOpenGL(window, true);
        }
        else if(type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            return ImGui_ImplGlfw_InitForVulkan(window, true);
        }
        return false;
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
