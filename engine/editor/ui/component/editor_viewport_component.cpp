#include "editor/ui/component/editor_viewport_component.h"

#include "editor/platform/editor_imgui_renderer.h"
#include "runtime/render/render_system.h"

namespace kpengine::editor
{
    EditorViewportComponent::EditorViewportComponent(render::RenderSystem *render_system,
                                                     IEditorImguiRenderer *imgui_renderer)
        : render_system_(render_system), imgui_renderer_(imgui_renderer)
    {
    }

    void EditorViewportComponent::Render()
    {
        if (!render_system_ || !imgui_renderer_)
        {
            ImGui::TextUnformatted("Scene viewport unavailable.");
            return;
        }

        const ImVec2 available_size = ImGui::GetContentRegionAvail();
        if (available_size.x <= 0.0f || available_size.y <= 0.0f)
        {
            return;
        }

        render_system_->RequestSceneRenderTargetExtent(
            static_cast<uint32_t>(available_size.x), static_cast<uint32_t>(available_size.y));

        const graphics::RenderTargetView view =
            render_system_->GetSceneRenderTarget().GetView();
        const ImTextureID texture_id = imgui_renderer_->GetTextureID(view);
        if (!view.IsValid() || !texture_id)
        {
            ImGui::TextUnformatted("Scene viewport is not ready for this graphics API.");
            return;
        }

        imgui_renderer_->DrawSceneImage(texture_id, available_size);
    }
}
