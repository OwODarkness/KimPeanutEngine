#include "editor/platform/editor_imgui_opengl_renderer.h"
#include <stdexcept>
#include <imgui_impl_opengl3.h>
#include <glad/glad.h>
#include "log/logger.h"
#include "graphics/backend/opengl/opengl_editor_bridge.h"
namespace kpengine::editor
{
    constexpr const char* LogName = "EditorImguiOpenglRendererLog";
    namespace
    {
        void EnableFramebufferSrgb(const ImDrawList *, const ImDrawCmd *)
        {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }

        void DisableFramebufferSrgb(const ImDrawList *, const ImDrawCmd *)
        {
            glDisable(GL_FRAMEBUFFER_SRGB);
        }
    }
    bool EditorImguiOpenglRenderer::Initialize(
        graphics::IEditorPresentationBridge *presentation_bridge)
    {
        const auto *const opengl_bridge = dynamic_cast<graphics::OpenglEditorBridge *>(
            presentation_bridge);
        if (opengl_bridge == nullptr)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "OpenGL editor presentation bridge is unavailable");
            throw std::runtime_error("OpenGL editor presentation bridge is unavailable");
        }

        // glad's proc table is loaded by the legacy GL backend, which isn't in the
        // build yet — load it here so the editor's own GL calls (glClear) work.
        if (gladLoadGL() == 0)
        {
            throw std::runtime_error("Failed to load OpenGL functions for Editor UI");
        }

        if (!ImGui_ImplOpenGL3_Init("#version 450"))
        {
            throw std::runtime_error("Failed to initialize ImGui OpenGL renderer");
        }
        imgui_backend_initialized_ = true;
        return true;
    }

    void EditorImguiOpenglRenderer::Shutdown()
    {
        if (imgui_backend_initialized_)
        {
            ImGui_ImplOpenGL3_Shutdown();
            imgui_backend_initialized_ = false;
        }
    }

    void EditorImguiOpenglRenderer::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
    }

    void EditorImguiOpenglRenderer::Render()
    {
        glClearColor(background_color_.r, background_color_.g,
                     background_color_.b, background_color_.a);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void EditorImguiOpenglRenderer::SetBackgroundColor(const LogColor &color)
    {
        background_color_ = color;
    }

    ImTextureID EditorImguiOpenglRenderer::GetTextureID(const graphics::RenderTargetView &view)
    {
        return view.IsValid()
                   ? static_cast<ImTextureID>(view.native_image_view)
                   : ImTextureID{};
    }

    void EditorImguiOpenglRenderer::DrawSceneImage(ImTextureID texture_id, const ImVec2 &size)
    {
        // Sampling the sRGB scene texture decodes it to linear. Re-enable the
        // matching write conversion only for this image command; ordinary ImGui
        // colors continue to render to the default framebuffer unchanged.
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCallback(EnableFramebufferSrgb, nullptr);
        ImGui::Image(texture_id, size);
        draw_list->AddCallback(DisableFramebufferSrgb, nullptr);
    }

}
