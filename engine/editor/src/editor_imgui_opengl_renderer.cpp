#include "editor_imgui_opengl_renderer.h"
#include <imgui/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include "log/logger.h"
namespace kpengine::editor
{
    constexpr const char* LogName = "EditorImguiOpenglRendererLog";
    void EditorImguiOpenglRenderer::Initialize(GraphicsContext context)
    {
        if(context.type != GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Graphics api mismatch, current type is not OpenGL");
            throw std::runtime_error("Graphics api mismatch, current type is not OpenGL");
        }

        ImGui_ImplOpenGL3_Init("#version 450");
    }

    void EditorImguiOpenglRenderer::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
    }

    void EditorImguiOpenglRenderer::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
    }

    void EditorImguiOpenglRenderer::Render()
    {

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

}