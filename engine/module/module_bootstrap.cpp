#include "module/module_bootstrap.h"

#include "editor/ui/editor_extension_registry.h"
#include "runtime/engine.h"

#if KPENGINE_ENABLE_LIVE2D
#include "module/live2d/editor/live2d_editor_registration.h"
#include "module/live2d/live2d_module.h"
#endif

#include <memory>

namespace kpengine::module
{
    void RegisterModules(kpengine::runtime::Engine &engine)
    {
#if KPENGINE_ENABLE_LIVE2D
        // The application creates the module. Engine becomes its owner and
        // invokes OnRegister/Initialize/Tick/Shutdown thereafter.
        engine.RegisterModule(std::make_unique<kpengine::live2d::Live2DModule>());

        // Editor integration is also supplied by the module, but uses the
        // editor's generic registry instead of coupling EditorUI to Live2D.
        kpengine::live2d::editor::RegisterEditorExtensions(
            kpengine::editor::GetEditorExtensionRegistry());
#else
        (void)engine;
#endif
    }
}
