#include "editor_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/scene_system.h"
#include "editor/include/editor_scene_manager.h"
namespace kpengine{
    namespace editor{
        EditorContext global_editor_context;


        void EditorContext::Initialize(const EditorContextInitInfo& init_info)
        {
            window_system_ = init_info.window_system;
            scene_system_ = init_info.scene_system;

            editor_scene_manager_ = new EditorSceneManager();
            editor_scene_manager_->Initialize(scene_system_->scene_.get());
        }

        void EditorContext::Clear()
        {
            delete editor_scene_manager_;
        }
    }
}
