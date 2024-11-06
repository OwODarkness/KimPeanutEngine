#include "editor_scene_manager.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/render_scene.h"
#include "editor/include/editor_global_context.h"
namespace kpengine
{
    namespace editor
    {

        void EditorSceneManager::Initialize()
        {
            scene_ui_ = std::make_shared<ui::EditorSceneComponent>(global_editor_context.render_system_->GetRenderScene()->scene_.get());
        }

        void EditorSceneManager::Tick()
        {
            scene_ui_->Render();
        }

        void EditorSceneManager::Close()
        {
            scene_ui_.reset();
        }

        bool EditorSceneManager::IsCursorInScene(float cursor_x, float cursor_y)
        {
            bool is_inside_x_bounding = cursor_x >= scene_ui_->pos_x && cursor_x <= scene_ui_->pos_x + scene_ui_->width_;
            bool is_inside_y_bounding = cursor_y >= scene_ui_->pos_y && cursor_y <= scene_ui_->pos_y + scene_ui_->height_;
            if(is_inside_x_bounding && is_inside_y_bounding)
            {
                return true;
            }
            return false;
        }

        bool EditorSceneManager::IsSCeneFocus() const
        {
            return scene_ui_->is_scene_window_focus;
        }
    }
}