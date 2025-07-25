#include "editor_scene_component.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "runtime/render/frame_buffer.h"
#include "editor/include/editor_global_context.h"
#include "runtime/core/system/window_system.h"
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
            GLFWwindow *window = editor::global_editor_context.window_system_->GetOpenGLWndow();
            // ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            // ImGui::SetNextFrameWantCaptureMouse(true);
            ImGui::BeginChild("render target");

            // Render the scene texture
            ImGui::Image(
                static_cast<ImTextureID>(scene_->GetTexture()),
                ImGui::GetContentRegionAvail(),
                ImVec2(0, 1),
                ImVec2(1, 0));

            // Only act on mouse click if the region is hovered
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    is_scene_window_focus = true;
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