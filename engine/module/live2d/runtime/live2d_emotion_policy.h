#ifndef KPENGINE_LIVE2D_EMOTION_POLICY_H
#define KPENGINE_LIVE2D_EMOTION_POLICY_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "live2d_product.h"

namespace kpengine::live2d
{
    inline constexpr std::uint32_t kLive2DEmotionPolicySchemaVersion = 1u;

    struct Live2DPolicyMotionReference final
    {
        std::string group;
        std::uint32_t index = 0u;
    };

    struct Live2DBehaviorIntent final
    {
        std::string emotion;
        std::string body_language;
    };

    struct Live2DSemanticState final
    {
        std::string id;
        Live2DBehaviorIntent intent;
        bool idle = false;
    };

    struct Live2DAuthoredBehaviorBinding final
    {
        std::string state_id;
        std::optional<Live2DPolicyMotionReference> motion;
        std::optional<std::string> expression;
    };

    struct Live2DEmotionPolicy final
    {
        std::uint32_t schema_version = kLive2DEmotionPolicySchemaVersion;
        std::string fallback_state_id;
        std::vector<Live2DSemanticState> states;
        std::vector<Live2DAuthoredBehaviorBinding> bindings;
    };

    const Live2DSemanticState *FindLive2DSemanticState(
        const Live2DEmotionPolicy &policy, std::string_view state_id) noexcept;

    const Live2DAuthoredBehaviorBinding *FindLive2DAuthoredBinding(
        const Live2DEmotionPolicy &policy, std::string_view state_id) noexcept;

    bool ValidateLive2DEmotionPolicy(const Live2DEmotionPolicy &policy,
                                     const Live2DProductData &product,
                                     std::string &diagnostic);
}

#endif
