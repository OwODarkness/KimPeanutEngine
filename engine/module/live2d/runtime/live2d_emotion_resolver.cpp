#include "live2d_emotion_resolver.h"

#include <algorithm>
#include <exception>
#include <limits>

namespace kpengine::live2d
{
    std::unique_ptr<Live2DEmotionResolver> Live2DEmotionResolver::Create(
        std::shared_ptr<const Live2DEmotionPolicy> policy,
        const Live2DProductData &product, std::string &diagnostic)
    {
        diagnostic.clear();
        if (policy == nullptr)
        {
            diagnostic = "Live2D emotion resolver policy is unavailable";
            return nullptr;
        }
        if (!ValidateLive2DEmotionPolicy(*policy, product, diagnostic))
        {
            return nullptr;
        }
        try
        {
            return std::unique_ptr<Live2DEmotionResolver>(
                new Live2DEmotionResolver(std::move(policy)));
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Live2D emotion resolver creation failed: ") +
                         error.what();
            return nullptr;
        }
    }

    const Live2DSemanticState *Live2DEmotionResolver::FindIntentState(
        const Live2DBehaviorIntent &intent) const noexcept
    {
        const auto found = std::find_if(
            policy_->states.begin(), policy_->states.end(),
            [&intent](const Live2DSemanticState &state) {
                return state.intent.emotion == intent.emotion &&
                       state.intent.body_language == intent.body_language;
            });
        return found == policy_->states.end() ? nullptr : &*found;
    }

    bool Live2DEmotionResolver::FillBehavior(
        const std::string_view state_id,
        Live2DResolvedBehavior &behavior) const
    {
        const Live2DAuthoredBehaviorBinding *binding =
            FindLive2DAuthoredBinding(*policy_, state_id);
        if (binding == nullptr)
        {
            return false;
        }
        behavior = {};
        behavior.state_id.assign(state_id.data(), state_id.size());
        behavior.motion = binding->motion;
        behavior.expression = binding->expression;
        return true;
    }

    bool Live2DEmotionResolver::Resolve(
        const Live2DBehaviorRequest &request,
        Live2DBehaviorResolution &resolution,
        std::string &diagnostic)
    {
        diagnostic.clear();
        try
        {
            resolution = {};
            if (request.priority <= 0)
            {
                diagnostic = "Live2D behavior priority must be positive";
                return false;
            }

            const Live2DSemanticState *matched_state = FindIntentState(request.intent);
            const std::string &target_state_id =
                matched_state != nullptr ? matched_state->id : policy_->fallback_state_id;
            resolution.used_fallback = matched_state == nullptr;
            resolution.previous_state_id = current_state_id_;
            resolution.priority = request.priority;
            resolution.blend_mode = request.blend_mode;

            if (!FillBehavior(target_state_id, resolution.behavior))
            {
                diagnostic = "Live2D emotion policy binding is unavailable for state: " +
                             target_state_id;
                resolution = {};
                return false;
            }

            const bool same_state = current_state_id_ == target_state_id;
            if (!request.force && !same_state && !current_state_id_.empty() &&
                request.priority < current_priority_)
            {
                resolution.reason = Live2DBehaviorTransitionReason::LowerPriorityRejected;
                return true;
            }
            if (same_state && !request.retrigger)
            {
                resolution.accepted = true;
                resolution.reason = Live2DBehaviorTransitionReason::SameStateIgnored;
                resolution.priority = current_priority_;
                resolution.transition_sequence = transition_sequence_;
                return true;
            }

            resolution.accepted = true;
            resolution.changed = true;
            resolution.reason = current_state_id_.empty()
                                    ? (resolution.used_fallback
                                           ? Live2DBehaviorTransitionReason::Fallback
                                           : Live2DBehaviorTransitionReason::Initial)
                                    : same_state
                                          ? Live2DBehaviorTransitionReason::Retriggered
                                          : resolution.used_fallback
                                                ? Live2DBehaviorTransitionReason::Fallback
                                                : Live2DBehaviorTransitionReason::IntentChanged;
            if (transition_sequence_ == std::numeric_limits<std::uint64_t>::max())
            {
                diagnostic = "Live2D emotion transition sequence is exhausted";
                resolution = {};
                return false;
            }
            ++transition_sequence_;
            current_state_id_ = target_state_id;
            current_priority_ = request.priority;
            resolution.transition_sequence = transition_sequence_;
            return true;
        }
        catch (const std::exception &error)
        {
            resolution = {};
            diagnostic = std::string("Live2D emotion resolution failed: ") +
                         error.what();
            return false;
        }
    }

    void Live2DEmotionResolver::Reset() noexcept
    {
        current_state_id_.clear();
        current_priority_ = 0;
        transition_sequence_ = 0u;
    }
}
