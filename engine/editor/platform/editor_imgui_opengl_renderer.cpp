#include "editor/platform/editor_imgui_opengl_renderer.h"
#include <imgui_impl_opengl3.h>
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

        // glad's proc table is loaded by the legacy GL backend, which isn't in the
        // build yet — load it here so the editor's own GL calls (glClear) work.
        gladLoadGL();

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
        // Stopgap: the legacy scene renderer (which cleared the frame) is being
        // rebuilt, so the editor owns the clear until it returns.
        glClearColor(0.1f, 0.1f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

}