#include "editor_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "editor/include/editor_scene_manager.h"
#include "editor/include/editor_input_manager.h"
#include "editor/include/editor_log_manager.h"
namespace kpengine{
    namespace editor{
        EditorContext global_editor_context;


        void EditorContext::Initialize(const EditorContextInitInfo& init_info)
        {
            window_system_ = init_info.window_system;
            render_system_ = init_info.render_system;
            runtime_engine_ = init_info.runtime_engine;

            editor_scene_manager_ = new EditorSceneManager();
            editor_scene_manager_->Initialize();

            editor_input_manager_ = new EditorInputManager();
            editor_input_manager_->Initialize();

            editor_log_manager = new EditorLogManager();
            editor_log_manager->Initialize();
        }

        void EditorContext::Clear()
        {
            delete editor_scene_manager_;
            delete editor_input_manager_;
            delete editor_log_manager;
        }
    }
}
