#include "editor/platform/editor_imgui_opengl_renderer.h"
#include <array>
#include <stdexcept>
#include <utility>
#include <imgui_impl_opengl3.h>
#include <glad/glad.h>
#include "log/logger.h"
#include "graphics/backend/opengl/opengl_editor_bridge.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
namespace kpengine::editor
{
    constexpr const char* LogName = "EditorImguiOpenglRendererLog";
    void EditorUiBeginSrgbImageCallback(const ImDrawList *, const ImDrawCmd *)
    {
        glEnable(GL_FRAMEBUFFER_SRGB);
    }

    void EditorUiEndSrgbImageCallback(const ImDrawList *, const ImDrawCmd *)
    {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    bool EditorImguiOpenglRenderer::Initialize(
        graphics::IEditorPresentationBridge *presentation_bridge)
    {
        auto *const opengl_bridge = dynamic_cast<graphics::OpenglEditorBridge *>(
            presentation_bridge);
        if (opengl_bridge == nullptr)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "OpenGL editor presentation bridge is unavailable");
            throw std::runtime_error("OpenGL editor presentation bridge is unavailable");
        }
        presentation_bridge_ = opengl_bridge;

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
        rhi_renderer_.Initialize(presentation_bridge_);
        return true;
    }

    void EditorImguiOpenglRenderer::Shutdown()
    {
        if (imgui_backend_initialized_)
        {
            ImGui_ImplOpenGL3_Shutdown();
            imgui_backend_initialized_ = false;
        }
        presentation_bridge_ = nullptr;
        rhi_renderer_.Shutdown();
    }

    void EditorImguiOpenglRenderer::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
    }

    void EditorImguiOpenglRenderer::Render()
    {
        ImDrawData *const draw_data = ImGui::GetDrawData();
        const std::array<float, 4> clear_color{
            background_color_.r, background_color_.g,
            background_color_.b, background_color_.a};
        std::string rhi_diagnostic;
        if (rhi_renderer_.Render(draw_data, clear_color, &rhi_diagnostic))
        {
            return;
        }
        if (!rhi_diagnostic.empty() && rhi_diagnostic != last_rhi_diagnostic_)
        {
            KP_LOG(LogName, LOG_LEVEL_WARNING,
                   "Common Editor UI rendering fell back to native OpenGL: %s",
                   rhi_diagnostic.c_str());
            last_rhi_diagnostic_ = std::move(rhi_diagnostic);
        }
        EditorGlowPacket glow_packet;
        std::string glow_diagnostic;
        if (!BuildEditorGlowPacket(draw_data, glow_packet, &glow_diagnostic))
        {
            if (glow_diagnostic != last_glow_diagnostic_)
            {
                KP_LOG(LogName, LOG_LEVEL_WARNING,
                       "Ignoring invalid editor glow packet: %s", glow_diagnostic.c_str());
                last_glow_diagnostic_ = std::move(glow_diagnostic);
            }
        }
        else
        {
            last_glow_diagnostic_.clear();
        }

        const bool recorded = presentation_bridge_ != nullptr &&
            presentation_bridge_->ExecuteRhiFrame(
                [draw_data, &clear_color](graphics::RenderBackend &,
                                          graphics::CommandRecorder &recorder)
                {
                    if (!recorder.BeginPresentation(&clear_color))
                    {
                        return false;
                    }
                    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
                    recorder.EndRenderTarget();
                    return true;
                });
        if (!recorded)
        {
            glClearColor(background_color_.r, background_color_.g,
                         background_color_.b, background_color_.a);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(draw_data);
        }
    }

    void EditorImguiOpenglRenderer::SetBackgroundColor(const LogColor &color)
    {
        background_color_ = color;
        rhi_renderer_.SetBackgroundColor(color);
    }

    void EditorImguiOpenglRenderer::SetBloomShaders(const data::ShaderData *vertex,
                                                    const data::ShaderData *fragment)
    {
        rhi_renderer_.SetBloomShaders(vertex, fragment);
    }

    ImTextureID EditorImguiOpenglRenderer::GetTextureID(const graphics::RenderTargetView &view)
    {
        const ImTextureID texture_id = view.IsValid()
                                           ? static_cast<ImTextureID>(view.native_image_view)
                                           : ImTextureID{};
        graphics::RenderTargetView ui_view = view;
        // DrawSceneImage already reverses V for OpenGL render-target textures.
        // Keep the adapter from applying the same origin correction a second time.
        ui_view.origin = graphics::TextureOrigin::TopLeft;
        rhi_renderer_.RegisterTexture(texture_id, ui_view);
        return texture_id;
    }

    void EditorImguiOpenglRenderer::DrawSceneImage(ImTextureID texture_id, const ImVec2 &size)
    {
        // Sampling the sRGB scene texture decodes it to linear. Re-enable the
        // matching write conversion only for this image command; ordinary ImGui
        // colors continue to render to the default framebuffer unchanged.
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCallback(EditorUiBeginSrgbImageCallback, nullptr);
        // OpenGL render-target textures use a bottom-left image origin while
        // ImGui's image UV convention starts at the top-left. Reverse only
        // the V range here; scene rendering and asset texture UVs stay API
        // neutral.
        ImGui::Image(texture_id, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        draw_list->AddCallback(EditorUiEndSrgbImageCallback, nullptr);
    }

}
