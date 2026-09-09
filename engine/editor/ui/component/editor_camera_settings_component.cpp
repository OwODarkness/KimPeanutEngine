#include "editor/ui/component/editor_camera_settings_component.h"

#include <imgui.h>

#include "runtime/runtime_camera_control.h"

namespace kpengine::editor
{
    EditorCameraSettingsComponent::EditorCameraSettingsComponent(
        runtime::ISceneCameraControlSink *camera_control_sink)
        : EditorWindowComponent("Camera Settings", EditorWindowConfig{0.8f, 0.0f, 0.2f,
                                                                        0.35f, true}),
          camera_control_sink_(camera_control_sink)
    {
    }

    void EditorCameraSettingsComponent::RenderContent()
    {
        if (camera_control_sink_ == nullptr)
        {
            ImGui::TextDisabled("Camera control unavailable");
            return;
        }

        if (!initialized_)
        {
            move_speed_ = camera_control_sink_->GetSceneCameraMoveSpeed();
            initialized_ = true;
        }

        ImGui::TextDisabled("Applies to free camera movement");
        if (ImGui::DragFloat(
                "Move Speed", &move_speed_, 1.0f,
                runtime::kMinimumSceneCameraMoveSpeed,
                runtime::kMaximumSceneCameraMoveSpeed,
                "%.1f units/s", ImGuiSliderFlags_AlwaysClamp))
        {
            camera_control_sink_->SetSceneCameraMoveSpeed(move_speed_);
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            move_speed_ = runtime::kDefaultSceneCameraMoveSpeed;
            camera_control_sink_->SetSceneCameraMoveSpeed(move_speed_);
        }
    }
}
