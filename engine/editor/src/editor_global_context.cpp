#include "editor_global_context.h"
#include "window_system.h"
namespace kpengine{
    namespace editor{
        EditorContext global_editor_context;


        void EditorContext::Initialize()
        {
            window_system = new kpengine::WindowSystem();
            window_system->Initialize(kpengine::WindowInitInfo::GetDefaultWindowInfo());
        }

        void EditorContext::Clear()
        {
            delete window_system;
        }
    }
}
