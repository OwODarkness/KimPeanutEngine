#include "editor_scene_component.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "runtime/render/frame_buffer.h"
#include "editor/include/editor_global_context.h"
#include "runtime/core/system/window_system.h"
namespace kpengine{
    namespace ui{
        EditorSceneComponent::EditorSceneComponent(FrameBuffer* scene):
        scene_(scene)
        {
            width_ = scene->width_;
            height_ = scene->height_;
        }

        void EditorSceneComponent::SetTitle(const std::string & title)
        {
            title_ = title;
        }
        void EditorSceneComponent::Render()
        {
            ImGui::Begin(title_.c_str());
            {
                //ImGui::SetMouseCursor(ImGuiMouseCursor_None);
                //ImGui::SetNextFrameWantCaptureMouse(true);
                //

                pos_x = ImGui::GetWindowPos().x;
                pos_y = ImGui::GetWindowPos().y;
                width_ = (int)ImGui::GetContentRegionAvail().x;
                height_ = (int)ImGui::GetContentRegionAvail().y;
                ImGui::BeginChild("render target");
                ImGui::Image(
                    (ImTextureID)scene_->GetTexture(),
                    ImGui::GetContentRegionAvail(),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                );
                is_scene_window_focus = ImGui::IsWindowFocused();
                if(is_scene_window_focus)
                {
                    glfwSetInputMode(editor::global_editor_context.window_system_->GetOpenGLWndow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    
                }
                else
                {
                    glfwSetInputMode(editor::global_editor_context.window_system_->GetOpenGLWndow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }
    }
}