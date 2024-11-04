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
    }
}