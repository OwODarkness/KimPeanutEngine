#include "editor_scene_manager.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"
namespace kpengine{
    namespace editor{


        void EditorSceneManager::Initialize()
        {
            scene_ui_ = std::make_shared<ui::EditorSceneComponent>(scene_);
        }

        void EditorSceneManager::Tick()
        {
            scene_ui_->Render();   
        }

        void EditorSceneManager::Close()
        {
            scene_ui_.reset();
        }
    }
}