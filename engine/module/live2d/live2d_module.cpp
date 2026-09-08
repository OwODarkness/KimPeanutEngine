#include "live2d_module.h"

#include "log/log_system.h"
#include "log/logger.h"

namespace kpengine::live2d
{
    void Live2DModule::OnRegister(kpengine::runtime::Engine &engine)
    {
        engine_ = &engine;
    }

    bool Live2DModule::Initialize(kpengine::runtime::Engine &engine)
    {
        engine_ = &engine;
        if (system_.Initialize())
        {
            return true;
        }

        KP_LOG("Live2DModule", LOG_LEVEL_ERROR,
               "Cubism framework initialization failed");
        return false;
    }

    void Live2DModule::Tick(float delta_time)
    {
        system_.Tick(delta_time);
    }

    void Live2DModule::Shutdown() noexcept
    {
        system_.Shutdown();
        engine_ = nullptr;
    }
}
