#ifndef KPENGINE_LIVE2D_MODEL_INSTANCE_H
#define KPENGINE_LIVE2D_MODEL_INSTANCE_H

#include <cstddef>
#include <memory>

namespace kpengine::live2d
{
    class CubismLifecycle;
    class Live2DModelResource;

    // A per-owner mutable Cubism model built from one immutable Asset payload.
    // Cubism headers stay private to the implementation so the module's public
    // contract does not expose SDK allocation or framework types.
    class Live2DModelInstance final
    {
    public:
        ~Live2DModelInstance() noexcept;

        Live2DModelInstance(const Live2DModelInstance &) = delete;
        Live2DModelInstance &operator=(const Live2DModelInstance &) = delete;

        Live2DModelInstance(Live2DModelInstance &&) noexcept;
        Live2DModelInstance &operator=(Live2DModelInstance &&) noexcept;

        bool IsValid() const noexcept;
        const Live2DModelResource &Resource() const noexcept;

        std::size_t ParameterCount() const noexcept;
        bool GetParameterRange(std::size_t index, float &minimum,
                               float &maximum) const noexcept;
        bool GetParameterValue(std::size_t index, float &value) const noexcept;
        bool SetParameterValue(std::size_t index, float value) noexcept;
        bool Update() noexcept;

    private:
        struct Impl;

        Live2DModelInstance(std::shared_ptr<const Live2DModelResource> resource,
                            std::unique_ptr<Impl> impl) noexcept;
        static std::unique_ptr<Live2DModelInstance> Create(
            std::shared_ptr<const Live2DModelResource> resource,
            CubismLifecycle &lifecycle);

        std::shared_ptr<const Live2DModelResource> resource_;
        std::unique_ptr<Impl> impl_;

        friend class Live2DSystem;
    };
}

#endif
