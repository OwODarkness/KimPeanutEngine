#ifndef KPENGINE_LIVE2D_SYSTEM_H
#define KPENGINE_LIVE2D_SYSTEM_H

#include <memory>

#include "asset/asset.h"
#include "live2d_cubism_lifecycle.h"
#include "live2d_model_instance.h"

namespace kpengine::live2d
{
    // Live2D's runtime service. It owns Cubism's process/framework lifetime
    // and creates per-owner model instances; rendering remains a later stage.
    class Live2DSystem final
    {
    public:
        Live2DSystem() = default;
        ~Live2DSystem() noexcept;

        Live2DSystem(const Live2DSystem&) = delete;
        Live2DSystem& operator=(const Live2DSystem&) = delete;

        bool Initialize();
        void Tick(float delta_time);
        void Shutdown() noexcept;

        bool IsInitialized() const noexcept;
        std::unique_ptr<Live2DModelInstance> CreateInstance(
            const asset::AssetID &asset_id);
        CubismLifecycle& Cubism() noexcept { return cubism_; }
        const CubismLifecycle& Cubism() const noexcept { return cubism_; }

    private:
        CubismLifecycle cubism_;
    };
}

#endif
