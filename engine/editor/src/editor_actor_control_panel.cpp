#include "editor/include/editor_actor_control_panel.h"

#include "runtime/game_framework/actor.h"
#include "component/mesh_component.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_mesh.h"
#include "editor/include/editor_ui_component/editor_slider_component.h"
#include "editor/include/editor_ui_component/editor_text_component.h"
#include "editor/include/editor_ui_component/editor_drag_component.h"
#include "editor/include/editor_global_context.h"
#include "runtime/core/system/window_system.h"

namespace kpengine
{
    namespace ui
    {
        EditorActorControlPanel::EditorActorControlPanel() : EditorWindowComponent("Actor Control Panel"),
                                                             actor_(nullptr), last_actor_(nullptr)
        {
            // MeshComponent* meshcomp =  dynamic_cast<MeshComponent*>(actor_->GetRootComponent());
            // std::shared_ptr<RenderMesh> mesh = meshcomp->GetMesh();
            // std::shared_ptr<RenderMaterial> material = mesh->GetMaterial(0);
            // if(material)
            // {
            //     AddComponent(std::make_shared<EditorTextComponent>(actor_->GetName()));
            //     float* roughness_ref = material->GetFloatParamRef(material_param_type::ROUGHNESS_PARAM);
            //     float* metallic_ref = material->GetFloatParamRef(material_param_type::METALLIC_PARAM);
            //     if(roughness_ref)
            //     {
            //         AddComponent(std::make_shared<EditorSliderComponent<float>>("roughness", roughness_ref, 0.05f, 1.f));
            //     }
            //     if(metallic_ref)
            //     {
            //         AddComponent(std::make_shared<EditorSliderComponent<float>>("metallic", metallic_ref, 0.f, 1.f));
            //     }

            // }
        }

        void EditorActorControlPanel::RenderContent()
        {
            if (actor_)
            {
                GLFWwindow *window = editor::global_editor_context.window_system_->GetOpenGLWndow();

                if (actor_ != last_actor_)
                {
                    Vector3f new_location = actor_->GetActorLocation();
                    location[0] = new_location[0];
                    location[1] = new_location[1];
                    location[2] = new_location[2];

                    Rotatorf new_rotation = actor_->GetActorRotation();
                    rotation[0] = new_rotation.pitch_;
                    rotation[1] = new_rotation.yaw_;
                    rotation[2] = new_rotation.roll_;

                    Vector3f new_scale = actor_->GetActorScale();
                    scale[0] = new_scale[0];
                    scale[1] = new_scale[1];
                    scale[2] = new_scale[2];

                    last_actor_ = actor_;
                }

                ImGui::Text("object name: ");
                ImGui::SameLine();
                ImGui::TextUnformatted(actor_->GetName().c_str());

                if (DragFloat3WithLock("location", location, 0.1f, -999.f, 999.f, window))
                {
                    actor_->SetActorLocation({location[0], location[1], location[2]});
                }

                if (DragFloat3WithLock("rotation", rotation, 0.2f, -180.f, 180.f, window))
                {
                    actor_->SetActorRotation({rotation[0], rotation[1], rotation[2]});
                }

                if (DragFloat3WithLock("scale", scale, 0.1f, -100.f, 100.f, window))
                {
                    actor_->SetActorScale({scale[0], scale[1], scale[2]});
                }

                EditorWindowComponent::RenderContent();
            }
        }

        void EditorActorControlPanel::SetActor(std::shared_ptr<Actor> actor)
        {
            actor_ = actor;
        }

        bool EditorActorControlPanel::DragFloat3WithLock(const char *label, float v[3], float speed, float min, float max, GLFWwindow *window)
        {
            bool changed = ImGui::DragFloat3(label, v, speed, min, max);

            if(ImGui::IsItemHovered())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }

            if (ImGui::IsItemActive())
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            else if (!ImGui::IsAnyItemActive())
            {
                // Only unlock when no item is active at all
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
            return changed;
        }

    }
}