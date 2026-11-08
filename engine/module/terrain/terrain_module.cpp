#include "terrain_module.h"

#include "runtime/engine.h"

namespace kpengine::terrain
{
    void TerrainModule::OnRegister(runtime::Engine &engine)
    {
        (void)engine;
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
