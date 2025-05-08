#include "editor/include/editor_actor_control_panel.h"

#include "runtime/game_framework/actor.h"
#include "component/mesh_component.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_mesh.h"
#include "editor/include/editor_ui_component/editor_slider_component.h"
#include "editor/include/editor_ui_component/editor_text_component.h"

namespace kpengine{
    namespace ui{
        EditorActorControlPanel::EditorActorControlPanel(Actor* actor):
        EditorWindowComponent("Actor Control Panel"),
        actor_(actor)
        {
            MeshComponent* meshcomp =  dynamic_cast<MeshComponent*>(actor_->GetRootComponent());
            std::shared_ptr<RenderMesh> mesh = meshcomp->GetMesh();
            std::shared_ptr<RenderMaterial> material = mesh->GetMaterial(0);
            // if(material)
            // {
            //     AddComponent(new EditorTextComponent(actor_->GetName()));
            //     EditorSliderComponent<float>* roughness_slide = new EditorSliderComponent<float>("roughness", &material->roughness, 0.05f, 1.f);
            //     AddComponent(roughness_slide);

            //     EditorSliderComponent<float>* metallic_slide = new EditorSliderComponent<float>("metallic", &material->metallic, 0.f, 1.f);
            //     AddComponent(metallic_slide);
            // }
        }


    }
}