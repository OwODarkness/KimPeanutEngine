#ifndef KPENGINE_LIVE2D_SYSTEM_H
#define KPENGINE_LIVE2D_SYSTEM_H

#include "live2d_cubism_lifecycle.h"

namespace kpengine::live2d
{
    // Live2D's runtime service. It owns Cubism's process/framework lifetime;
    // model loading and rendering can be added behind this boundary later.
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
        CubismLifecycle& Cubism() noexcept { return cubism_; }
        const CubismLifecycle& Cubism() const noexcept { return cubism_; }

    private:
        CubismLifecycle cubism_;
    };
}

#endif
