#include "editor/include/editor_actor_control_panel.h"
#include <GLFW/glfw3.h>
#include "runtime/game_framework/actor.h"
#include "component/mesh_component.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_mesh.h"
#include "editor/include/editor_ui_component/editor_slider_component.h"
#include "editor/include/editor_ui_component/editor_text_component.h"
#include "editor/include/editor_ui_component/editor_drag_component.h"
#include "editor/include/editor_global_context.h"
#include "runtime/window/window_system.h"

namespace kpengine
{
    namespace ui
    {
        EditorActorControlPanel::EditorActorControlPanel() : EditorWindowComponent("Actor Control Panel"),
                                                             actor_(nullptr), last_actor_(nullptr)
        {

        }

        void EditorActorControlPanel::RenderContent()
        {
            if (actor_)
            {
                GLFWwindow *window = static_cast<GLFWwindow*>(editor::global_editor_context.window_system_->GetNativeHandle());

                    const Vector3f& new_location = actor_->GetActorLocation();
                    location[0] = new_location[0];
                    location[1] = new_location[1];
                    location[2] = new_location[2];

                    const Rotatorf& new_rotation = actor_->GetActorRotation();
                    rotation[0] = new_rotation.pitch_;
                    rotation[1] = new_rotation.yaw_;
                    rotation[2] = new_rotation.roll_;

                    const Vector3f& new_scale = actor_->GetActorScale();
                    scale[0] = new_scale[0];
                    scale[1] = new_scale[1];
                    scale[2] = new_scale[2];


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

            return changed;
        }

    }
}