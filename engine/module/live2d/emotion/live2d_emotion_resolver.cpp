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
            if (record_sequence_ == std::numeric_limits<std::uint64_t>::max())
            {
                diagnostic = "Live2D emotion transition record sequence is exhausted";
                resolution = {};
                return false;
            }
            const std::uint64_t previous_transition_sequence =
                transition_sequence_;
            const std::uint64_t next_transition_sequence =
                transition_sequence_ + 1u;
            resolution.transition_sequence = next_transition_sequence;
            AppendTransitionRecord(resolution);
            if (current_active_ && transition_history_.size() >= 2u &&
                transition_history_[transition_history_.size() - 2u]
                        .transition_sequence == previous_transition_sequence)
            {
                transition_history_[transition_history_.size() - 2u].terminal_state =
                    Live2DBehaviorTerminalState::Interrupted;
            }
            transition_sequence_ = next_transition_sequence;
            current_state_id_ = target_state_id;
            current_priority_ = request.priority;
            current_behavior_ = resolution.behavior;
            current_reason_ = resolution.reason;
            current_blend_mode_ = resolution.blend_mode;
            current_terminal_state_ = Live2DBehaviorTerminalState::Pending;
            current_active_ = true;
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

    bool Live2DEmotionResolver::CompleteActive(std::string &diagnostic)
    {
        return FinalizeActive(Live2DBehaviorTerminalState::Completed, diagnostic);
    }

    bool Live2DEmotionResolver::CancelActive(std::string &diagnostic)
    {
        return FinalizeActive(Live2DBehaviorTerminalState::Cancelled, diagnostic);
    }

    Live2DBehaviorSnapshot Live2DEmotionResolver::Snapshot() const
    {
        Live2DBehaviorSnapshot snapshot{};
        snapshot.active = current_active_;
        snapshot.state_id = current_state_id_;
        snapshot.behavior = current_behavior_;
        snapshot.priority = current_priority_;
        snapshot.transition_sequence = transition_sequence_;
        snapshot.reason = current_reason_;
        snapshot.blend_mode = current_blend_mode_;
        snapshot.terminal_state = current_terminal_state_;
        return snapshot;
    }

    std::vector<Live2DBehaviorTransitionRecord>
    Live2DEmotionResolver::TransitionHistory() const
    {
        return transition_history_;
    }

    bool Live2DEmotionResolver::FinalizeActive(
        const Live2DBehaviorTerminalState terminal_state,
        std::string &diagnostic)
    {
        diagnostic.clear();
        if (!current_active_)
        {
            diagnostic = "Live2D emotion resolver has no active behavior";
            return false;
        }
        if (transition_history_.empty())
        {
            diagnostic = "Live2D emotion resolver transition history is incomplete";
            return false;
        }
        current_active_ = false;
        current_terminal_state_ = terminal_state;
        transition_history_.back().terminal_state = terminal_state;
        return true;
    }

    void Live2DEmotionResolver::AppendTransitionRecord(
        const Live2DBehaviorResolution &resolution)
    {
        Live2DBehaviorTransitionRecord record{};
        record.record_sequence = record_sequence_ + 1u;
        record.transition_sequence = resolution.transition_sequence;
        record.previous_state_id = resolution.previous_state_id;
        record.behavior = resolution.behavior;
        record.priority = resolution.priority;
        record.used_fallback = resolution.used_fallback;
        record.reason = resolution.reason;
        record.blend_mode = resolution.blend_mode;
        transition_history_.push_back(std::move(record));
        if (transition_history_.size() >
            kLive2DEmotionTransitionHistoryCapacity)
        {
            transition_history_.erase(transition_history_.begin());
        }
        record_sequence_ = record.record_sequence;
    }

    void Live2DEmotionResolver::Reset() noexcept
    {
        current_state_id_.clear();
        current_priority_ = 0;
        transition_sequence_ = 0u;
        current_behavior_ = {};
        current_reason_ = Live2DBehaviorTransitionReason::SameStateIgnored;
        current_blend_mode_ = Live2DBehaviorBlendMode::AuthoredFadeOut;
        current_terminal_state_ = Live2DBehaviorTerminalState::Pending;
        current_active_ = false;
        record_sequence_ = 0u;
        transition_history_.clear();
    }
}
