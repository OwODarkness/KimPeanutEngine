#ifndef KPENGINE_LIVE2D_EMOTION_RESOLVER_H
#define KPENGINE_LIVE2D_EMOTION_RESOLVER_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "live2d_emotion_policy.h"

namespace kpengine::live2d
{
    enum class Live2DBehaviorBlendMode
    {
        AuthoredFadeOut,
        Immediate
    };

    enum class Live2DBehaviorTransitionReason
    {
        Initial,
        IntentChanged,
        Fallback,
        Retriggered,
        LowerPriorityRejected,
        SameStateIgnored
    };

    struct Live2DBehaviorRequest final
    {
        Live2DBehaviorIntent intent;
        std::int32_t priority = 1;
        bool force = false;
        bool retrigger = false;
        Live2DBehaviorBlendMode blend_mode =
            Live2DBehaviorBlendMode::AuthoredFadeOut;
    };

    struct Live2DResolvedBehavior final
    {
        std::string state_id;
        std::optional<Live2DPolicyMotionReference> motion;
        std::optional<std::string> expression;
    };

    struct Live2DBehaviorResolution final
    {
        bool accepted = false;
        bool changed = false;
        bool used_fallback = false;
        std::uint64_t transition_sequence = 0u;
        std::int32_t priority = 0;
        Live2DBehaviorTransitionReason reason =
            Live2DBehaviorTransitionReason::SameStateIgnored;
        std::string previous_state_id;
        Live2DResolvedBehavior behavior;
        Live2DBehaviorBlendMode blend_mode =
            Live2DBehaviorBlendMode::AuthoredFadeOut;
    };

    class Live2DEmotionResolver final
    {
    public:
        static std::unique_ptr<Live2DEmotionResolver> Create(
            std::shared_ptr<const Live2DEmotionPolicy> policy,
            const Live2DProductData &product, std::string &diagnostic);

        Live2DEmotionResolver(const Live2DEmotionResolver &) = delete;
        Live2DEmotionResolver &operator=(const Live2DEmotionResolver &) = delete;

        Live2DEmotionResolver(Live2DEmotionResolver &&) noexcept = default;
        Live2DEmotionResolver &operator=(Live2DEmotionResolver &&) noexcept = default;
        ~Live2DEmotionResolver() noexcept = default;

        bool Resolve(const Live2DBehaviorRequest &request,
                     Live2DBehaviorResolution &resolution,
                     std::string &diagnostic);

        void Reset() noexcept;
        const std::string &CurrentStateId() const noexcept
        {
            return current_state_id_;
        }
        std::int32_t CurrentPriority() const noexcept
        {
            return current_priority_;
        }
        std::uint64_t TransitionSequence() const noexcept
        {
            return transition_sequence_;
        }

    private:
        explicit Live2DEmotionResolver(
            std::shared_ptr<const Live2DEmotionPolicy> policy) noexcept
            : policy_(std::move(policy))
        {
        }

        const Live2DSemanticState *FindIntentState(
            const Live2DBehaviorIntent &intent) const noexcept;
        bool FillBehavior(std::string_view state_id,
                          Live2DResolvedBehavior &behavior) const;

        std::shared_ptr<const Live2DEmotionPolicy> policy_;
        std::string current_state_id_;
        std::int32_t current_priority_ = 0;
        std::uint64_t transition_sequence_ = 0u;
    };
}

#endif
