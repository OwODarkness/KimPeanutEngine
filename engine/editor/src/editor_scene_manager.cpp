#include "editor_scene_manager.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"
#include "runtime/core/system/scene_system.h"
namespace kpengine
{
    namespace editor
    {

        void EditorSceneManager::Initialize(SceneSystem *scene_system)
        {
            scene_system_ = scene_system;
            scene_ui_ = std::make_shared<ui::EditorSceneComponent>(scene_system->scene_.get());

        }

        void EditorSceneManager::Tick()
        {

            scene_system_->Render();
            scene_ui_->Render();
        }

        void EditorSceneManager::Close()
        {
            scene_ui_.reset();
        }
    }
}