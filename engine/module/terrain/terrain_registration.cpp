#include "module/module_registration.h"

#include <memory>

#include "runtime/engine.h"
#include "terrain_module.h"

namespace kpengine::terrain
{
    namespace
    {
        void RegisterTerrain(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() == runtime::ApplicationMode::TerrainViewer)
            {
                engine.RegisterModule(std::make_unique<TerrainModule>());
            }
        }

        const module::ModuleRegistration registration(&RegisterTerrain);
    }
}
