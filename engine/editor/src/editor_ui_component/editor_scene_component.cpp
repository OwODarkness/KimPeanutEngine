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
        void EditorSceneComponent::Render()
        {
            ImGui::Begin(title_.c_str());
            {
                ImGui::BeginChild("render target");
                ImGui::Image(
                    (ImTextureID)scene_->GetTexture(),
                    ImGui::GetContentRegionAvail(),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                );
                ImGui::EndChild();
            }
            ImGui::End();
        }
    }
}