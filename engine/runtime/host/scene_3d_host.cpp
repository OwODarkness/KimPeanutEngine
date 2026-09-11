#include "host/scene_3d_host.h"

#include "engine.h"
#include "runtime_global_context.h"

namespace kpengine::runtime
{
    bool Scene3DHost::Initialize(Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != ApplicationMode::Scene3D)
        {
            diagnostic = "3DSceneHost can only initialize in scene3d mode";
            return false;
        }

        global_runtime_context.InitializeSceneServices();
        if (!global_runtime_context.AreSceneServicesInitialized())
        {
            diagnostic = "3DSceneHost failed to initialize scene services";
            return false;
        }
        initialized_.store(true, std::memory_order_release);
        return true;
    }

    bool Scene3DHost::Tick(const float /*delta_time*/, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_.load(std::memory_order_acquire))
        {
            diagnostic = "3DSceneHost is not initialized";
            return false;
        }
        return true;
    }

    bool Scene3DHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_.load(std::memory_order_acquire))
        {
            diagnostic = "3DSceneHost is not initialized";
            return false;
        }
        return true;
    }

    void Scene3DHost::Shutdown() noexcept
    {
        initialized_.store(false, std::memory_order_release);
    }
}
