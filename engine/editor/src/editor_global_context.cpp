#include "editor_global_context.h"
#include "runtime/core/system/window_system.h"
namespace kpengine{
    namespace editor{
        EditorContext global_editor_context;


        void EditorContext::Initialize(const EditorContextInitInfo& init_info)
        {
            window_system_ = init_info.window_system;
        }

        void EditorContext::Clear()
        {
        }
    }
}
