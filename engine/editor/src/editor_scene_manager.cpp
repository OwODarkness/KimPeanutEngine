#include "editor_scene_manager.h"

#include "editor/include/editor_ui_component/editor_scene_component.h"
#include "editor/include/editor_actor_control_panel.h"
#include "editor/include/editor_global_context.h"
#include "runtime/input/input_context.h"
#include "runtime/core/system/input_system.h"
#include "runtime/core/system/world_system.h"

#include "runtime/game_framework/world.h"
#include "runtime/game_framework/level.h"
#include "runtime/game_framework/actor.h"
#include "runtime/component/mesh_component.h"

#include "runtime/render/render_system.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/render_scene.h"
#include "runtime/render/gizmos.h"
#include <limits>

namespace kpengine
{
    namespace editor
    {
        EditorSceneManager::EditorSceneManager() : scene_ui_(nullptr),
                                                   input_context_(nullptr),
                                                   object_selected_index(-1)
        {
        }
        void EditorSceneManager::Initialize()
        {
            scene_ui_ = std::make_unique<ui::EditorSceneComponent>(global_editor_context.render_system_->GetRenderScene()->scene_fb_.get());
            scene_ui_->on_mouse_click_callback_.Bind<EditorSceneManager, &EditorSceneManager::OnClickMouseCallback>(this);

            actor_panel_ = std::make_unique<ui::EditorActorControlPanel>();

            input_context_ = std::make_shared<input::InputContext>();
            editor::global_editor_context.input_system_->AddContext("SceneInputContext", input_context_);
        }

        void EditorSceneManager::Tick()
        {
            scene_ui_->Render();
            actor_panel_->Render();
            if (scene_ui_->is_scene_window_focus)
            {
                editor::global_editor_context.input_system_->SetActiveContext("SceneInputContext");
            }
            else
            {
                editor::global_editor_context.input_system_->SetActiveContext("");
            }

            float mouse_pos_x = scene_ui_->GetMousePosX();
            float mouse_pos_y = scene_ui_->GetMousePosY();
            RenderCamera *camera = editor::global_editor_context.render_system_->GetRenderCamera();

            if (!camera)
                return;
            //If gizmos axis existed, check hit axis and control drag behavior
            if (gizmos_)
            {
                const Vector3f &origin_pos = camera->GetPosition();
                Vector3f world_ray = camera->ComputeWorldRayFromScreen({mouse_pos_x, mouse_pos_y}, {(float)scene_ui_->width_, (float)scene_ui_->height_});
                bool hit = gizmos_->HitAxis(origin_pos, world_ray);
                if (hit)
                {
                    if(scene_ui_->IsLeftMouseClick())
                    {
                        gizmos_->DragStart(origin_pos, world_ray);
                    }
                    if (scene_ui_->IsLeftMouseDrag())
                    {
                        gizmos_->Lock();
                        gizmos_->Drag(origin_pos, world_ray);
                    }
                }
                if (scene_ui_->IsLeftMouseRelease())
                {
                    gizmos_->UnLock();
                }
            }

        }

        void EditorSceneManager::Close()
        {
            scene_ui_.reset();
        }

        bool EditorSceneManager::IsCursorInScene(float cursor_x, float cursor_y)
        {
            bool is_inside_x_bounding = cursor_x >= scene_ui_->pos_x && cursor_x <= scene_ui_->pos_x + scene_ui_->width_;
            bool is_inside_y_bounding = cursor_y >= scene_ui_->pos_y && cursor_y <= scene_ui_->pos_y + scene_ui_->height_;
            if (is_inside_x_bounding && is_inside_y_bounding)
            {
                return true;
            }
            return false;
        }

        bool EditorSceneManager::IsSceneFocus() const
        {
            return scene_ui_->is_scene_window_focus;
        }

        void EditorSceneManager::ClearLastSelection()
        {
            if (last_select_actor_)
            {
                MeshComponent *last_mesh = dynamic_cast<MeshComponent *>(last_select_actor_->GetRootComponent());
                if (!last_mesh)
                    return;
                last_mesh->SetOutlineVisibility(false);
            }
            last_select_actor_ = nullptr;
            gizmos_ = nullptr;
        }

        void EditorSceneManager::OnClickMouseCallback(float mouse_pos_x, float mouse_pos_y)
        {
            RenderCamera *camera = editor::global_editor_context.render_system_->GetRenderCamera();

            if (!camera)
                return;
            // transform the click position and camera direction to world direction
            Vector3f origin_pos = camera->GetPosition();
            Vector3f world_ray = camera->ComputeWorldRayFromScreen({mouse_pos_x, mouse_pos_y}, {(float)scene_ui_->width_, (float)scene_ui_->height_});

            auto world = editor::global_editor_context.world_system_->GetCurrentWorld().lock();
            if (!world)
                return;
            auto level = world->GetCurrentLevel().lock();
            if (!level)
                return;

            std::vector<std::shared_ptr<Actor>> actors = level->GetLevelActors();
            std::shared_ptr<Actor> selected = nullptr;
            float closest_dist = std::numeric_limits<float>::max();

            for (const auto &item : actors)
            {
                MeshComponent *mesh = dynamic_cast<MeshComponent *>(item->GetRootComponent());
                if (!mesh)
                    continue;
                float hit_dist = 0;
                bool should_update_selected = mesh->GetWorldAABB().Intersect(origin_pos, world_ray, hit_dist) && closest_dist > hit_dist;
                if (should_update_selected)
                {
                    closest_dist = hit_dist;
                    selected = item;
                }
            }

            if (last_select_actor_ == selected)
                return;

            ClearLastSelection();

            if (selected)
            {
                last_select_actor_ = selected;

                MeshComponent *mesh = dynamic_cast<MeshComponent *>(selected->GetRootComponent());
                if (!mesh)
                    return;
                mesh->SetOutlineVisibility(true);

                gizmos_ = std::make_shared<Gizmos>();
                gizmos_->Initialize();

                gizmos_->SetActor(selected);

                editor::global_editor_context.render_system_->SetVisibleAxis(gizmos_);
            }
            else
            {
                editor::global_editor_context.render_system_->SetVisibleAxis(nullptr);
                ClearLastSelection();
            }

            actor_panel_->SetActor(selected);
        }

        EditorSceneManager::~EditorSceneManager()
        {
            scene_ui_->on_mouse_click_callback_.UnBind();
        }
    }

}