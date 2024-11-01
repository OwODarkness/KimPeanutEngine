#include "editor_scene_manager.h"
#include "editor/include/editor_ui_component/editor_scene_component.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/render_scene.h"
namespace kpengine
{
    namespace editor
    {

        void EditorSceneManager::Initialize(RenderSystem *render_system)
        {
            render_system_ = render_system;
            scene_ui_ = std::make_shared<ui::EditorSceneComponent>(render_system->GetRenderScene()->scene_.get());

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