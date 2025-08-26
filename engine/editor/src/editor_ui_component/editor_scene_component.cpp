#include "editor_scene_component.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "runtime/render/frame_buffer.h"
#include "editor/include/editor_global_context.h"
#include "runtime/window/window_system.h"
namespace kpengine
{
    namespace ui
    {
        EditorSceneComponent::EditorSceneComponent(FrameBuffer *scene) : EditorWindowComponent("scene"),
                                                                         scene_(scene)
        {
            width_ = scene->width_;
            height_ = scene->height_;
        }

        void EditorSceneComponent::SetTitle(const std::string &title)
        {
            title_ = title;
        }
        void EditorSceneComponent::RenderContent()
        {
            EditorWindowComponent::RenderContent();
            GLFWwindow *window = static_cast<GLFWwindow*>(editor::global_editor_context.window_system_->GetNativeHandle());
            // ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            // ImGui::SetNextFrameWantCaptureMouse(true);
            ImGui::BeginChild("render target");

            // Render the scene texture
            ImGui::Image(
                static_cast<ImTextureID>(scene_->GetTexture(0)),
                ImGui::GetContentRegionAvail(),
                ImVec2(0, 1),
                ImVec2(1, 0));
            
            ImVec2 screen_size =   ImGui::GetCursorScreenPos();
            ImVec2 mouse_abs_pos = ImGui::GetMousePos();
            
            mouse_pos_x_ = mouse_abs_pos.x - screen_size.x;
            mouse_pos_y_ = screen_size.y - mouse_abs_pos.y;


            // Only act on mouse click if the region is hovered
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            {
                is_left_mouse_drag_ =  ImGui::IsMouseDragging(ImGuiMouseButton_Left);
                is_left_mouse_down_ =  ImGui::IsMouseDown(ImGuiMouseButton_Left);
                is_left_mouse_released_ = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                is_left_mouse_clicked_ =  ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    is_scene_window_focus = true;
                }
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    on_mouse_click_callback_.ExecuteIfBound(mouse_pos_x_, mouse_pos_y_);
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                is_scene_window_focus = false;
            }

            ImGui::EndChild();
        }

        EditorSceneComponent::~EditorSceneComponent()
        {
        }
    }
}