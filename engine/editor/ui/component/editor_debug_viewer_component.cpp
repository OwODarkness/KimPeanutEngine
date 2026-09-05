#include "editor/ui/component/editor_debug_viewer_component.h"

#include "editor/platform/editor_imgui_renderer.h"
#include "runtime/render/render_system.h"

#include <algorithm>

namespace
{
    ImVec2 FitRenderTarget(const kpengine::graphics::RenderTargetView &view,
                           const ImVec2 &available)
    {
        if (!view.IsValid() || available.x <= 0.0f || available.y <= 0.0f)
        {
            return available;
        }

        const float target_aspect = static_cast<float>(view.width) /
                                    static_cast<float>(view.height);
        const float available_aspect = available.x / available.y;
        if (available_aspect > target_aspect)
        {
            return ImVec2(available.y * target_aspect, available.y);
        }
        return ImVec2(available.x, available.x / target_aspect);
    }

    void CenterImage(const ImVec2 &available, const ImVec2 &image_size)
    {
        ImGui::SetCursorPos(ImVec2(
            ImGui::GetCursorPosX() + std::max(0.0f, (available.x - image_size.x) * 0.5f),
            ImGui::GetCursorPosY() + std::max(0.0f, (available.y - image_size.y) * 0.5f)));
    }

    struct DebugViewOption
    {
        kpengine::render::CaptureView view;
        const char *label;
    };

    constexpr DebugViewOption kDebugViewOptions[] = {
        {kpengine::render::CaptureView::SceneColor, "Scene Color"},
        {kpengine::render::CaptureView::WorldNormal, "World Normal"},
        {kpengine::render::CaptureView::LinearDepth, "Linear Depth"},
        {kpengine::render::CaptureView::BaseColor, "Base Color"},
        {kpengine::render::CaptureView::MaterialParams, "Material Parameters"},
        {kpengine::render::CaptureView::ShadowVisibility, "Shadow Visibility"},
        {kpengine::render::CaptureView::SpotShadowDepth, "Spot Shadow Depth"},
        {kpengine::render::CaptureView::SpotShadowVisibility, "Spot Shadow Visibility"},
        {kpengine::render::CaptureView::PointShadowDepth, "Point Shadow Depth"},
        {kpengine::render::CaptureView::PointShadowVisibility, "Point Shadow Visibility"},
    };

    const char *DebugViewLabel(kpengine::render::CaptureView view)
    {
        for (const DebugViewOption &option : kDebugViewOptions)
        {
            if (option.view == view)
            {
                return option.label;
            }
        }
        return "World Normal";
    }
}

namespace kpengine::editor
{
    EditorDebugViewerComponent::EditorDebugViewerComponent(
        render::RenderSystem *render_system, IEditorImguiRenderer *imgui_renderer)
        : EditorWindowComponent("Debug Viewer", EditorWindowConfig{0.8f, 0.0f, 0.2f, 0.7f, true}),
          render_system_(render_system), imgui_renderer_(imgui_renderer)
    {
        if (render_system_ != nullptr)
        {
            render_system_->SetDebugView(debug_view_);
        }
    }

    void EditorDebugViewerComponent::RenderContent()
    {
        if (!render_system_ || !imgui_renderer_)
        {
            return;
        }

        ImGui::TextUnformatted("Diagnostic Preview");
        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float preview_width = std::max(1.0f, available.x);
        const float preview_height = std::clamp(preview_width * 9.0f / 16.0f, 72.0f,
                                                std::max(72.0f, available.y - 86.0f));
        ImGui::BeginChild("##DebugViewerPreview", ImVec2(preview_width, preview_height), true,
                          ImGuiWindowFlags_NoScrollbar);
        const graphics::RenderTargetView debug_target =
            render_system_->GetDebugRenderTargetView();
        const ImTextureID texture_id = imgui_renderer_->GetTextureID(debug_target);
        if (!debug_target.IsValid() || !texture_id)
        {
            ImGui::TextUnformatted("Debug preview is not ready.");
        }
        else
        {
            const ImVec2 image_area = ImGui::GetContentRegionAvail();
            const ImVec2 image_size = FitRenderTarget(debug_target, image_area);
            CenterImage(image_area, image_size);
            imgui_renderer_->DrawSceneImage(texture_id, image_size);
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextUnformatted("Output");
        if (ImGui::BeginCombo("##DebugViewerOutput", DebugViewLabel(debug_view_)))
        {
            for (const DebugViewOption &option : kDebugViewOptions)
            {
                const bool selected = debug_view_ == option.view;
                if (ImGui::Selectable(option.label, selected))
                {
                    debug_view_ = option.view;
                    render_system_->SetDebugView(debug_view_);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Use the lock button in the title bar to pin this 20%% window, or unlock it to move and resize.");
    }
}
