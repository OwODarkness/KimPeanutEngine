#ifndef KPENGINE_LIVE2D_MODULE_H
#define KPENGINE_LIVE2D_MODULE_H

#include "module/engine_module.h"
#include "module/live2d/runtime/live2d_system.h"

namespace kpengine::live2d
{
    class Live2DModule final : public kpengine::module::EngineModule
    {
    public:
        const char *Name() const noexcept override { return "Live2D"; }
        void OnRegister(kpengine::runtime::Engine &engine) override;
        bool Initialize(kpengine::runtime::Engine &engine) override;
        void Tick(float delta_time) override;
        void Shutdown() noexcept override;

        Live2DSystem& System() noexcept { return system_; }
        const Live2DSystem& System() const noexcept { return system_; }

    private:
        kpengine::runtime::Engine *engine_ = nullptr;
        Live2DSystem system_;
    };
}

#endif
