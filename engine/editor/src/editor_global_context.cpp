#include "editor_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/render/render_system.h"
#include "runtime/core/log/log_system.h"

#include "editor/include/editor_scene_manager.h"
#include "editor/include/editor_log_manager.h"
namespace kpengine{
    namespace editor{
        EditorContext global_editor_context;


        EditorContext::EditorContext():
        editor_scene_manager_(new EditorSceneManager()),
        editor_log_manager_(new EditorLogManager())
        {

        }

        void EditorContext::Initialize(const EditorContextInitInfo& init_info)
        {
            window_system_ = init_info.window_system;
            render_system_ = init_info.render_system;
            log_system_ = init_info.log_system;
            world_system_ = init_info.world_system;
            input_system_ = init_info.input_system;
            runtime_engine_ = init_info.runtime_engine;

            graphics_backend_type_ = init_info.graphics_backend_type;

            editor_scene_manager_->Initialize();

            editor_log_manager_->Initialize(log_system_);
        }

        void EditorContext::Clear()
        {
            delete editor_scene_manager_;
            editor_scene_manager_ = nullptr;
            delete editor_log_manager_;
        }


    }
}
