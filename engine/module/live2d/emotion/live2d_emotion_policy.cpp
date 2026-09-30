#include "live2d_emotion_policy.h"

#include <algorithm>
#include <set>

namespace kpengine::live2d
{
    namespace
    {
        constexpr std::size_t kMaxPolicyEntries = 64u;
        constexpr std::size_t kMaxPolicyFieldBytes = 256u;

        bool IsValidField(const std::string &value, const bool allow_empty = false) noexcept
        {
            return (allow_empty || !value.empty()) &&
                   value.size() <= kMaxPolicyFieldBytes &&
                   value.find('\0') == std::string::npos;
        }

        bool ContainsMotion(const Live2DProductData &product,
                            const Live2DPolicyMotionReference &reference) noexcept
        {
            return std::any_of(
                product.motions.begin(), product.motions.end(),
                [&reference](const Live2DAuthoredMotion &motion) {
                    return motion.group == reference.group &&
                           motion.index == reference.index;
                });
        }

        bool ContainsExpression(const Live2DProductData &product,
                                const std::string &name) noexcept
        {
            return std::any_of(
                product.expressions.begin(), product.expressions.end(),
                [&name](const Live2DAuthoredExpression &expression) {
                    return expression.name == name;
                });
        }
    }

    const Live2DSemanticState *FindLive2DSemanticState(
        const Live2DEmotionPolicy &policy, const std::string_view state_id) noexcept
    {
        const auto found = std::find_if(
            policy.states.begin(), policy.states.end(),
            [state_id](const Live2DSemanticState &state) {
                return state.id == state_id;
            });
        return found == policy.states.end() ? nullptr : &*found;
    }

    const Live2DAuthoredBehaviorBinding *FindLive2DAuthoredBinding(
        const Live2DEmotionPolicy &policy, const std::string_view state_id) noexcept
    {
        const auto found = std::find_if(
            policy.bindings.begin(), policy.bindings.end(),
            [state_id](const Live2DAuthoredBehaviorBinding &binding) {
                return binding.state_id == state_id;
            });
        return found == policy.bindings.end() ? nullptr : &*found;
    }

    bool ValidateLive2DEmotionPolicy(const Live2DEmotionPolicy &policy,
                                     const Live2DProductData &product,
                                     std::string &diagnostic)
    {
        diagnostic.clear();
        if (policy.schema_version != kLive2DEmotionPolicySchemaVersion)
        {
            diagnostic = "Live2D emotion policy schema version is unsupported";
            return false;
        }
        if (product.product_version < 2u)
        {
            diagnostic = "Live2D emotion policy requires Product V2 typed playback data";
            return false;
        }
        if (policy.states.empty() || policy.states.size() > kMaxPolicyEntries ||
            policy.bindings.size() != policy.states.size())
        {
            diagnostic = "Live2D emotion policy state and binding counts are invalid";
            return false;
        }
        if (!IsValidField(policy.fallback_state_id) ||
            FindLive2DSemanticState(policy, policy.fallback_state_id) == nullptr)
        {
            diagnostic = "Live2D emotion policy fallback state is invalid";
            return false;
        }

        std::set<std::string> state_ids;
        const Live2DSemanticState *fallback = nullptr;
        for (const Live2DSemanticState &state : policy.states)
        {
            if (!IsValidField(state.id) || !IsValidField(state.intent.emotion) ||
                !IsValidField(state.intent.body_language) ||
                !state_ids.insert(state.id).second)
            {
                diagnostic = "Live2D emotion policy contains an invalid or duplicate state";
                return false;
            }
            if (state.id == policy.fallback_state_id)
            {
                fallback = &state;
            }
        }
        if (fallback == nullptr || !fallback->idle)
        {
            diagnostic = "Live2D emotion policy fallback state must be idle";
            return false;
        }

        std::set<std::string> binding_ids;
        for (const Live2DAuthoredBehaviorBinding &binding : policy.bindings)
        {
            if (!IsValidField(binding.state_id) ||
                FindLive2DSemanticState(policy, binding.state_id) == nullptr ||
                !binding_ids.insert(binding.state_id).second)
            {
                diagnostic = "Live2D emotion policy contains an invalid binding state";
                return false;
            }
            if (!binding.motion.has_value() && !binding.expression.has_value())
            {
                diagnostic = "Live2D emotion policy binding has no authored behavior";
                return false;
            }
            if (binding.motion.has_value() &&
                (!IsValidField(binding.motion->group) ||
                 !ContainsMotion(product, *binding.motion)))
            {
                diagnostic = "Live2D emotion policy references a missing motion";
                return false;
            }
            if (binding.expression.has_value() &&
                (!IsValidField(*binding.expression) ||
                 !ContainsExpression(product, *binding.expression)))
            {
                diagnostic = "Live2D emotion policy references a missing expression";
                return false;
            }
        }
        return true;
    }
}
