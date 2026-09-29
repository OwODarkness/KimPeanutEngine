#include "terrain_module.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "runtime/engine.h"
#include "tools/terrain_viewer_host.h"

namespace kpengine::terrain
{
    void TerrainModule::OnRegister(runtime::Engine &engine)
    {
        if (engine.GetApplicationMode() != runtime::ApplicationMode::TerrainViewer)
        {
            return;
        }

        std::string diagnostic;
        if (!engine.RegisterApplicationHostProvider(
                runtime::ApplicationMode::TerrainViewer,
                [](runtime::Engine &)
                {
                    return std::make_unique<TerrainViewerHost>();
                },
                diagnostic))
        {
            throw std::runtime_error("Terrain viewer host registration failed: " +
                                     diagnostic);
        }
    }

    bool TerrainModule::Initialize(runtime::Engine &)
    {
        return true;
    }

    void TerrainModule::Tick(float)
    {
    }

    void TerrainModule::Shutdown() noexcept
    {
    }
}
