#include "editor_scene_manager.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/render_scene.h"
#include "editor/include/editor_global_context.h"
#include "runtime/input/input_context.h"
#include "runtime/core/system/input_system.h"

namespace kpengine
{
    namespace editor
    {
        EditorSceneManager::EditorSceneManager() : scene_ui_(nullptr)
        {
        }
        void EditorSceneManager::Initialize()
        {
            scene_ui_ = std::make_unique<ui::EditorSceneComponent>(global_editor_context.render_system_->GetRenderScene()->scene_.get());
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

        bool EditorSceneManager::IsSCeneFocus() const
        {
            return scene_ui_->is_scene_window_focus;
        }

        EditorSceneManager::~EditorSceneManager() = default;
    }
}