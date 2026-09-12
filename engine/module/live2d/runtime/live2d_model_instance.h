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

    struct Live2DPlaybackToken final
    {
        std::uint64_t instance_serial = 0u;
        std::uint64_t sequence = 0u;
    };

    enum class Live2DMotionStartMode
    {
        RespectPriority,
        Force
    };

    enum class Live2DStopMode
    {
        AuthoredFadeOut,
        Immediate
    };

    enum class Live2DPlaybackEventKind
    {
        MotionCompleted,
        MotionInterrupted,
        MotionCancelled,
        MotionUserEvent
    };

    struct Live2DPlaybackEvent final
    {
        Live2DPlaybackEventKind kind = Live2DPlaybackEventKind::MotionUserEvent;
        Live2DPlaybackToken token{};
        std::string value;
    };

    struct Live2DPlaybackUpdateResult final
    {
        std::vector<Live2DPlaybackEvent> events;
        bool motion_parameters_updated = false;
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

        bool PlayMotion(const Live2DMotionKey &key, std::int32_t priority,
                        Live2DMotionStartMode mode,
                        Live2DPlaybackToken &token,
                        std::string &diagnostic);
        bool StopMotion(Live2DPlaybackToken token, Live2DStopMode mode,
                        std::string &diagnostic);
        void StopAllMotions(Live2DStopMode mode) noexcept;
        bool SetExpression(std::string_view name, std::string &diagnostic);
        bool ClearExpression(Live2DStopMode mode, std::string &diagnostic);
        bool AdvancePlayback(float delta_seconds,
                             Live2DPlaybackUpdateResult &result,
                             std::string &diagnostic);

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
