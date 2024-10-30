#include "editor_scene_component.h"
#include "runtime/render/frame_buffer.h"
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

        bool EditorSceneComponent::ShouldResize(float new_width, float new_height)
        {
                if(new_width != width_ || new_height != height_)
                {
                    width_ = new_width;
                    height_ = new_height;
                    scene_->ReSizeFrameBuffer(width_, new_height);
                    return true;
                }
            return false;
        }
        void EditorSceneComponent::Render()
        {
            ImGui::Begin(title_.c_str());
            {
                if(ShouldResize(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()))
                {
                    ImGui::End();
                    return ;
                }
                ImGui::BeginChild("render target");
                ImGui::Image(
                    (ImTextureID)scene_->GetTexture(),
                    ImVec2(width_, height_),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                );
                ImGui::EndChild();
            }
            ImGui::End();
        }
    }
}