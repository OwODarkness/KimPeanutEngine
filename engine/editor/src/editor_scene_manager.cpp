#include "editor_scene_manager.h"

#include <iostream>
#include "editor/include/editor_ui_component/editor_scene_component.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/render_scene.h"
#include "editor/include/editor_global_context.h"
#include "runtime/input/input_context.h"
#include "runtime/core/system/input_system.h"
#include "runtime/core/system/world_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/render_camera.h"
#include "runtime/game_framework/world.h"
#include "runtime/game_framework/level.h"
#include "runtime/game_framework/actor.h"
#include "runtime/component/mesh_component.h"
#include "runtime/render/render_axis.h"
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

            input_context_ = std::make_shared<input::InputContext>();
            editor::global_editor_context.input_system_->AddContext("SceneInputContext", input_context_);
        }

        void EditorSceneManager::Tick()
        {
            scene_ui_->Render();
            if (scene_ui_->is_scene_window_focus)
            {
                editor::global_editor_context.input_system_->SetActiveContext("SceneInputContext");
            }
            else
            {
                editor::global_editor_context.input_system_->SetActiveContext("");
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

        bool EditorSceneManager::IntersectRayAABB(const Vector3f &origin, const Vector3f &ray_dir, const AABB &aabb, float &out_dist)
        {
            const Vector3f &min = aabb.min_;
            const Vector3f &max = aabb.max_;

            float tmin = (min.x_ - origin.x_) / ray_dir.x_;
            float tmax = (max.x_ - origin.x_) / ray_dir.x_;

            if (tmin > tmax)
                std::swap(tmin, tmax);

            float tymin = (min.y_ - origin.y_) / ray_dir.y_;
            float tymax = (max.y_ - origin.y_) / ray_dir.y_;

            if (tymin > tymax)
                std::swap(tymin, tymax);

            if ((tmin > tymax) || (tymin > tmax))
                return false;

            if (tymin > tmin)
                tmin = tymin;
            if (tymax < tmax)
                tmax = tymax;

            float tzmin = (min.z_ - origin.z_) / ray_dir.z_;
            float tzmax = (max.z_ - origin.z_) / ray_dir.z_;

            if (tzmin > tzmax)
                std::swap(tzmin, tzmax);

            if ((tmin > tzmax) || (tzmin > tmax))
                return false;

            if (tzmin > tmin)
                tmin = tzmin;
            if (tzmax < tmax)
                tmax = tzmax;

            // Optional: discard negative intersections (behind camera)
            if (tmin < 0.0f && tmax < 0.0f)
                return false;

            out_dist = tmin >= 0.0f ? tmin : tmax;
            return true;
        }

        void EditorSceneManager::OnClickMouseCallback(float mouse_pos_x, float mouse_pos_y)
        {
            RenderCamera *camera = editor::global_editor_context.render_system_->GetRenderCamera();

            if (!camera)
                return;

            float camera_fov = camera->GetCameraFOV();
            Vector3f camera_forward = camera->GetCameraForward();
            Vector3f camera_up = camera->GetCameraUp();
            Vector3f camera_right = camera->GetCameraRight();
            Vector3f origin_pos = camera->GetPosition();
            float window_size_x = static_cast<float>(scene_ui_->width_);
            float window_size_y = static_cast<float>(scene_ui_->height_);
            float window_forward = 0.5f * window_size_y / std::tan(math::DegreeToRadian(camera_fov) * 0.5f);
            float window_right = mouse_pos_x - 0.5f * window_size_x;
            float window_up = mouse_pos_y - 0.5f * window_size_y;
            Vector3f world_ray_dir = camera_right * window_right + camera_up * window_up + camera_forward * window_forward;

            std::weak_ptr<World> world = editor::global_editor_context.world_system_->GetCurrentWorld();
            if (world.expired())
                return;
            std::weak_ptr<Level> level = world.lock()->GetCurrentLevel();
            if (level.expired())
                return;

            std::vector<std::shared_ptr<Actor>> actors = level.lock()->GetLevelActors();
            std::shared_ptr<Actor> selected = nullptr;
            float closest_dist = std::numeric_limits<float>::max();

            for (const auto &item : actors)
            {
                MeshComponent *mesh = dynamic_cast<MeshComponent *>(item->GetRootComponent());
                if (!mesh)
                    continue;
                float hit_dist;
                if (IntersectRayAABB(origin_pos, world_ray_dir, mesh->GetWorldAABB(), hit_dist))
                {
                    if (closest_dist > hit_dist)
                    {
                        closest_dist = hit_dist;
                        selected = item;
                    }
                }
            }

            if (selected)
            {
                if (last_select_actor_ == selected)
                {
                    return;
                }
                if (last_select_actor_)
                {
                    MeshComponent *last_mesh = dynamic_cast<MeshComponent *>(last_select_actor_->GetRootComponent());
                    if (!last_mesh)
                        return;
                    last_mesh->SetOutlineVisibility(false);
                }
                last_select_actor_ = selected;
                MeshComponent *mesh = dynamic_cast<MeshComponent *>(selected->GetRootComponent());
                if (!mesh)
                    return;
                std::shared_ptr<RenderAxis> axis = std::make_shared<RenderAxis>();
                axis->Initialize();
                Transform3f transform;
                transform.position_ = mesh->GetWorldAABB().Center();
                transform.rotator_ = selected->GetActorRotation();
                transform.scale_ = Vector3f(1.f);
                axis->SetModelTransform(Matrix4f::MakeTransformMatrix(transform).Transpose());
                editor::global_editor_context.render_system_->SetVisibleAxis(axis);
                mesh->SetOutlineVisibility(true);
            }
            else
            {
                editor::global_editor_context.render_system_->SetVisibleAxis(nullptr);
                if (last_select_actor_)
                {
                    MeshComponent *last_mesh = dynamic_cast<MeshComponent *>(last_select_actor_->GetRootComponent());
                    if (!last_mesh)
                        return;
                    last_mesh->SetOutlineVisibility(false);
                    last_select_actor_ = nullptr;
                }
            }
        }

        EditorSceneManager::~EditorSceneManager()
        {
            scene_ui_->on_mouse_click_callback_.UnBind();
        }
    }

}