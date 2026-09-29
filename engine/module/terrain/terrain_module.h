#ifndef KPENGINE_MODULE_TERRAIN_TERRAIN_MODULE_H
#define KPENGINE_MODULE_TERRAIN_TERRAIN_MODULE_H

#include "module/engine_module.h"

namespace kpengine::terrain
{
    class TerrainModule final : public module::EngineModule
    {
    public:
        const char *Name() const noexcept override { return "Terrain"; }
        void OnRegister(runtime::Engine &engine) override;
        bool Initialize(runtime::Engine &engine) override;
        void Tick(float delta_time) override;
        void Shutdown() noexcept override;
    };
}

#endif
