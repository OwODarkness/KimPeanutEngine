#ifndef KPENGINE_RUNTIME_HOST_SCENE_3D_HOST_H
#define KPENGINE_RUNTIME_HOST_SCENE_3D_HOST_H

#include <atomic>
#include <string>

#include "host/application_host.h"

namespace kpengine::runtime
{
    class Scene3DHost final : public IApplicationHost
    {
    public:
        const char *Name() const noexcept override { return "3d-scene"; }
        bool Initialize(Engine &engine, std::string &diagnostic) override;
        bool Tick(float delta_time, std::string &diagnostic) override;
        bool RecordFrame(std::string &diagnostic) override;
        void Shutdown() noexcept override;

    private:
        std::atomic_bool initialized_{false};
    };
}

#endif // KPENGINE_RUNTIME_HOST_SCENE_3D_HOST_H
