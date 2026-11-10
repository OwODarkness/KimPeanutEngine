#include "module/module_registration.h"

#include <memory>

#include "runtime/engine.h"
#include "runtime/host/application_host.h"
#include "runtime/launch_options.h"
#include "terrain_module.h"
#include "tools/terrain_viewer_host.h"

namespace kpengine::terrain
{
    namespace
    {
        void RegisterTerrain(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() == "terrain-viewer")
            {
                engine.RegisterModule(std::make_unique<TerrainModule>());
            }
        }

        const runtime::ApplicationHostRegistration host_registration{
            {"terrain-viewer",
             [](const runtime::RuntimeLaunchOptions &) { return std::make_unique<TerrainViewerHost>(); },
             runtime::ApplicationHostLayout::SharedWindowPresentation,
             true}};
        const module::ModuleRegistration registration(&RegisterTerrain);
    }
}
