#ifndef KPENGINE_LIVE2D_MODEL_INSTANCE_H
#define KPENGINE_LIVE2D_MODEL_INSTANCE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "asset/texture.h"
#include "live2d_model_data.h"

namespace kpengine::live2d
{
    class CubismLifecycle;
    class Live2DModelResource;

    struct Live2DMotionKey final
    {
        std::string group;
        std::uint32_t index = 0u;
    };

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
        std::uint64_t InstanceSerial() const noexcept;

        std::size_t MotionCount() const noexcept;
        bool HasMotion(const Live2DMotionKey &key) const noexcept;
        std::size_t ExpressionCount() const noexcept;
        bool HasExpression(std::string_view name) const noexcept;

        std::size_t ParameterCount() const noexcept;
        bool GetParameterRange(std::size_t index, float &minimum,
                               float &maximum) const noexcept;
        bool GetParameterValue(std::size_t index, float &value) const noexcept;
        bool SetParameterValue(std::size_t index, float value) noexcept;
        bool Update() noexcept;

        const std::vector<std::shared_ptr<const asset::TextureResource>> &
        TextureDependencies() const noexcept;

        bool ExtractStaticData(Live2DStaticModelData &out,
                               std::string &diagnostic) const;
        bool ExtractFrameSnapshot(Live2DFrameSnapshot &out,
                                  std::string &diagnostic);

    private:
        struct Impl;

        Live2DModelInstance(std::shared_ptr<const Live2DModelResource> resource,
                            std::vector<std::shared_ptr<const asset::TextureResource>>
                                texture_dependencies,
                            std::unique_ptr<Impl> impl) noexcept;
        static std::unique_ptr<Live2DModelInstance> Create(
            std::shared_ptr<const Live2DModelResource> resource,
            std::vector<std::shared_ptr<const asset::TextureResource>>
                texture_dependencies,
            std::uint64_t instance_serial,
            CubismLifecycle &lifecycle);

        static bool BuildClipLibrary(const Live2DModelResource &resource,
                                      Impl &impl,
                                      std::string &diagnostic);

        std::shared_ptr<const Live2DModelResource> resource_;
        std::vector<std::shared_ptr<const asset::TextureResource>>
            texture_dependencies_;
        std::unique_ptr<Impl> impl_;

        friend class Live2DSystem;
    };
}

#endif
