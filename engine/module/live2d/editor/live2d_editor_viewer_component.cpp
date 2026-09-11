#include "module/live2d/editor/live2d_editor_viewer_component.h"

#include <imgui.h>

#include "editor/platform/editor_imgui_renderer.h"
#include "runtime/render/render_system.h"

namespace
{
}

namespace kpengine::live2d::editor
{
    Live2DEditorViewerComponent::Live2DEditorViewerComponent(
        kpengine::render::RenderSystem *render_system,
        kpengine::editor::IEditorImguiRenderer *imgui_renderer)
        : kpengine::editor::EditorWindowComponent(
              "Live2D Viewer",
              kpengine::editor::EditorWindowConfig{0.72f, 0.0f, 0.28f, 0.62f, true}),
          render_system_(render_system),
          imgui_renderer_(imgui_renderer)
    {
    }

    void Live2DEditorViewerComponent::RenderContent()
    {
        ImGui::TextUnformatted("Hiyori Live2D preview");

        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x <= 0.0f || available.y <= 0.0f)
        {
            return;
        }

        ImGui::BeginChild("##Live2DViewerCanvas", available, true,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        if (render_system_ == nullptr || imgui_renderer_ == nullptr)
        {
            ImGui::TextDisabled("Live2D presentation is unavailable.");
        }
        else
        {
            const graphics::RenderTargetView view =
                render_system_->GetRenderExtensionOutputView("Live2D");
            const ImTextureID texture = imgui_renderer_->GetTextureID(view);
            if (!view.IsValid() || texture == ImTextureID{})
            {
                ImGui::TextDisabled("Waiting for Hiyori renderer...");
            }
            else
            {
                const ImVec2 inner = ImGui::GetContentRegionAvail();
                const float aspect = static_cast<float>(view.width) /
                                     static_cast<float>(view.height);
                const float width = std::min(inner.x, inner.y * aspect);
                const ImVec2 size(width, width / aspect);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (inner.x - size.x) * 0.5f);
                imgui_renderer_->DrawSceneImage(texture, size);
            }
        }
        ImGui::EndChild();

        ImGui::TextDisabled("720 x 960 render target | Cubism Core | masks enabled");
    }
}
