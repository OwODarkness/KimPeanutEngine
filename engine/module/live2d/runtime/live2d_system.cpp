#include "live2d_system.h"

namespace kpengine::live2d
{
    Live2DSystem::~Live2DSystem() noexcept
    {
        Shutdown();
    }

    bool Live2DSystem::Initialize()
    {
        return cubism_.Initialize();
    }

    void Live2DSystem::Tick(float delta_time)
    {
        // Keep the service on the engine's game-thread schedule. Model update
        // and render submission will be added here once runtime assets exist.
        (void)delta_time;
    }

    void Live2DSystem::Shutdown() noexcept
    {
        cubism_.Shutdown();
    }

    bool Live2DSystem::IsInitialized() const noexcept
    {
        return cubism_.IsInitialized();
    }
}
