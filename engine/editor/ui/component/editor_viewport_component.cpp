#include "editor/ui/component/editor_viewport_component.h"

#include "editor/actor/actor_editor_model.h"
#include "editor/gizmo/transform_gizmo.h"
#include "editor/platform/editor_imgui_renderer.h"
#include "runtime/input/input_system.h"
#include "runtime/render/render_system.h"
#include "runtime/runtime_camera_control.h"
#include "runtime/window/window_system.h"

#include <algorithm>
#include <optional>

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
}

namespace kpengine::editor
{
    EditorViewportComponent::EditorViewportComponent(render::RenderSystem *render_system,
                                                     IEditorImguiRenderer *imgui_renderer,
                                                     WindowSystem *window_system,
                                                     input::InputSystem *input_system,
                                                     runtime::ISceneCameraControlSink *camera_control_sink,
                                                     runtime::ISceneSelectionSink *scene_selection_sink,
                                                     ActorEditorModel *actor_model)
        : render_system_(render_system), imgui_renderer_(imgui_renderer),
          window_system_(window_system), input_system_(input_system),
          camera_control_sink_(camera_control_sink), scene_selection_sink_(scene_selection_sink)
    {
        if (actor_model != nullptr && render_system_ != nullptr)
        {
            transform_gizmo_ = std::make_unique<EditorTransformGizmo>(*actor_model,
                                                                       *render_system_);
        }
    }

    EditorViewportComponent::~EditorViewportComponent()
    {
        SetCameraCapture(false);
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

        ImGui::BeginChild("##ViewportImage", available_size, false);
        render_system_->RequestSceneRenderTargetExtent(
            static_cast<uint32_t>(available_size.x), static_cast<uint32_t>(available_size.y));

        const graphics::RenderTargetView view = render_system_->GetSceneRenderTargetView();
        const ImTextureID texture_id = imgui_renderer_->GetTextureID(view);
        if (!view.IsValid() || !texture_id)
        {
            ImGui::TextUnformatted("Scene viewport is not ready for this graphics API.");
        }
        else
        {
            const ImVec2 image_size = FitRenderTarget(view, available_size);
            CenterImage(available_size, image_size);
            imgui_renderer_->DrawSceneImage(texture_id, image_size);
            const ImVec2 image_min = ImGui::GetItemRectMin();
            const bool gizmo_consumed_mouse = transform_gizmo_ != nullptr &&
                                               transform_gizmo_->Render(image_min, image_size, view);

            if (!camera_capture_active_)
            {
                if (ImGui::IsItemHovered() && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    SetCameraCapture(true);
                }
                else if (!gizmo_consumed_mouse && ImGui::IsItemHovered() &&
                         ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    RequestScenePick(image_min, image_size, view);
                }
            }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                SetCameraCapture(false);
            }
        }
        ImGui::EndChild();
    }

    void EditorViewportComponent::RequestScenePick(
        const ImVec2 &image_min, const ImVec2 &image_size,
        const graphics::RenderTargetView &view) const
    {
        if (scene_selection_sink_ == nullptr || render_system_ == nullptr ||
            image_size.x <= 0.0f || image_size.y <= 0.0f || !view.IsValid())
        {
            return;
        }

        const ImVec2 mouse_position = ImGui::GetIO().MousePos;
        const float image_x = (mouse_position.x - image_min.x) / image_size.x;
        const float image_y = (mouse_position.y - image_min.y) / image_size.y;
        if (image_x < 0.0f || image_x > 1.0f || image_y < 0.0f || image_y > 1.0f)
        {
            return;
        }

        const float ndc_x = image_x * 2.0f - 1.0f;
        const float ndc_y = 1.0f - image_y * 2.0f;
        const float aspect = static_cast<float>(view.width) /
                             static_cast<float>(view.height);
        const std::optional<spatial::Ray> ray =
            render_system_->BuildSceneRay(ndc_x, ndc_y, aspect);
        if (ray.has_value())
        {
            scene_selection_sink_->EnqueueScenePick(*ray);
        }
    }

    void EditorViewportComponent::SetCameraCapture(bool captured)
    {
        if (camera_capture_active_ == captured)
        {
            return;
        }

        camera_capture_active_ = captured;
        if (window_system_ != nullptr)
        {
            window_system_->SetMouseCapture(captured);
        }
        if (input_system_ != nullptr)
        {
            input_system_->ResetCursorTracking();
        }
        if (camera_control_sink_ != nullptr)
        {
            camera_control_sink_->SetSceneCameraControlCaptured(captured);
        }
    }
}
