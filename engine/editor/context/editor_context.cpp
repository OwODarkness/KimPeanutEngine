#include "editor/context/editor_context.h"
#include "runtime/window/window_system.h"
#include "runtime/render/render_system.h"
#include "runtime/core/log/log_system.h"
namespace kpengine{
    namespace editor{
        EditorContext global_editor_context;


        void EditorContext::Initialize(const EditorContextInitInfo& init_info)
        {
            window_system_ = init_info.window_system;
            render_system_ = init_info.render_system;
            log_system_ = init_info.log_system;
            input_system_ = init_info.input_system;
            runtime_engine_ = init_info.runtime_engine;
            memory_sampler_ = init_info.memory_sampler;

            graphics_api_type_ = init_info.graphics_api_type;
        }

        void EditorContext::Clear()
        {
            editor = nullptr;
        }


    }
}
