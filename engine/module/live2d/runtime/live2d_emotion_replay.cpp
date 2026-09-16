#include "live2d_emotion_replay.h"

#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace kpengine::live2d
{
    namespace
    {
        struct ReplayTrace final
        {
            std::vector<Live2DBehaviorSnapshot> snapshots;
            std::vector<Live2DBehaviorTransitionRecord> history;
        };

        bool EqualBehavior(const Live2DResolvedBehavior &left,
                           const Live2DResolvedBehavior &right) noexcept
        {
            if (left.state_id != right.state_id ||
                left.expression != right.expression ||
                left.motion.has_value() != right.motion.has_value())
            {
                return false;
            }
            return !left.motion.has_value() ||
                   (left.motion->group == right.motion->group &&
                    left.motion->index == right.motion->index);
        }

        bool EqualSnapshot(const Live2DBehaviorSnapshot &left,
                           const Live2DBehaviorSnapshot &right) noexcept
        {
            return left.active == right.active &&
                   left.state_id == right.state_id &&
                   EqualBehavior(left.behavior, right.behavior) &&
                   left.priority == right.priority &&
                   left.transition_sequence == right.transition_sequence &&
                   left.reason == right.reason &&
                   left.blend_mode == right.blend_mode &&
                   left.terminal_state == right.terminal_state;
        }

        bool EqualRecord(const Live2DBehaviorTransitionRecord &left,
                         const Live2DBehaviorTransitionRecord &right) noexcept
        {
            return left.record_sequence == right.record_sequence &&
                   left.transition_sequence == right.transition_sequence &&
                   left.previous_state_id == right.previous_state_id &&
                   EqualBehavior(left.behavior, right.behavior) &&
                   left.priority == right.priority &&
                   left.used_fallback == right.used_fallback &&
                   left.reason == right.reason &&
                   left.blend_mode == right.blend_mode &&
                   left.terminal_state == right.terminal_state;
        }

        bool BuildViewerPolicy(const Live2DProductData &product,
                               Live2DEmotionPolicy &policy,
                               std::string &diagnostic)
        {
            diagnostic.clear();
            if (product.product_version < 2u || product.motions.empty())
            {
                diagnostic =
                    "Live2D emotion replay requires Product V2 motion data";
                return false;
            }

            const Live2DAuthoredMotion &fallback_motion = product.motions.front();
            const Live2DAuthoredMotion &transition_motion =
                product.motions.size() > 1u ? product.motions[1u] : fallback_motion;
            const std::optional<std::string> expression =
                product.expressions.empty()
                    ? std::nullopt
                    : std::optional<std::string>{product.expressions.front().name};

            policy = {};
            policy.fallback_state_id = "neutral_idle";
            policy.states = {
                {"neutral_idle", {"neutral", "idle"}, true},
                {"joy_open", {"joy", "open"}, false},
                {"neutral_recovery", {"neutral", "recovery"}, false}};
            policy.bindings = {
                {"neutral_idle",
                 Live2DPolicyMotionReference{fallback_motion.group,
                                              fallback_motion.index},
                 expression},
                {"joy_open",
                 Live2DPolicyMotionReference{transition_motion.group,
                                              transition_motion.index},
                 expression},
                {"neutral_recovery",
                 Live2DPolicyMotionReference{fallback_motion.group,
                                              fallback_motion.index},
                 expression}};
            return ValidateLive2DEmotionPolicy(policy, product, diagnostic);
        }

        bool CaptureSnapshot(Live2DEmotionResolver &resolver,
                             ReplayTrace &trace,
                             std::string &diagnostic)
        {
            try
            {
                trace.snapshots.push_back(resolver.Snapshot());
                return true;
            }
            catch (const std::exception &error)
            {
                diagnostic = std::string("Live2D emotion replay snapshot failed: ") +
                             error.what();
                return false;
            }
        }

        bool RunSequence(const Live2DProductData &product, ReplayTrace &trace,
                         std::string &diagnostic)
        {
            Live2DEmotionPolicy policy;
            if (!BuildViewerPolicy(product, policy, diagnostic))
            {
                return false;
            }
            auto resolver = Live2DEmotionResolver::Create(
                std::make_shared<const Live2DEmotionPolicy>(std::move(policy)),
                product, diagnostic);
            if (resolver == nullptr)
            {
                return false;
            }

            Live2DBehaviorResolution resolution{};
            Live2DBehaviorRequest request{};
            request.intent = {"unknown", "unknown"};
            request.priority = 1;
            if (!resolver->Resolve(request, resolution, diagnostic) ||
                !resolution.accepted || !resolution.changed ||
                !resolution.used_fallback ||
                resolution.reason != Live2DBehaviorTransitionReason::Fallback ||
                !CaptureSnapshot(*resolver, trace, diagnostic))
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D emotion replay fallback step failed";
                }
                return false;
            }

            request.intent = {"joy", "open"};
            request.priority = 2;
            if (!resolver->Resolve(request, resolution, diagnostic) ||
                !resolution.accepted || !resolution.changed ||
                resolution.reason != Live2DBehaviorTransitionReason::IntentChanged ||
                !CaptureSnapshot(*resolver, trace, diagnostic))
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D emotion replay transition step failed";
                }
                return false;
            }

            request.intent = {"neutral", "recovery"};
            request.priority = 3;
            request.force = true;
            request.blend_mode = Live2DBehaviorBlendMode::Immediate;
            if (!resolver->Resolve(request, resolution, diagnostic) ||
                !resolution.accepted || !resolution.changed ||
                resolution.reason != Live2DBehaviorTransitionReason::IntentChanged ||
                !CaptureSnapshot(*resolver, trace, diagnostic))
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D emotion replay interruption step failed";
                }
                return false;
            }

            if (!resolver->CompleteActive(diagnostic) ||
                !CaptureSnapshot(*resolver, trace, diagnostic))
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D emotion replay recovery step failed";
                }
                return false;
            }
            trace.history = resolver->TransitionHistory();
            if (trace.history.size() != 3u ||
                trace.history[0u].terminal_state !=
                    Live2DBehaviorTerminalState::Interrupted ||
                trace.history[1u].terminal_state !=
                    Live2DBehaviorTerminalState::Interrupted ||
                trace.history[2u].terminal_state !=
                    Live2DBehaviorTerminalState::Completed)
            {
                diagnostic = "Live2D emotion replay terminal history is invalid";
                return false;
            }
            return true;
        }
    }

    bool RunLive2DEmotionReplay(const Live2DProductData &product,
                                Live2DEmotionReplayResult &result,
                                std::string &diagnostic)
    {
        diagnostic.clear();
        result = {};
        result.available = product.product_version >= 2u &&
                           !product.motions.empty();
        try
        {
            ReplayTrace first;
            if (!RunSequence(product, first, diagnostic))
            {
                result.diagnostic = diagnostic;
                return false;
            }
            ReplayTrace second;
            if (!RunSequence(product, second, diagnostic))
            {
                result.diagnostic = diagnostic;
                return false;
            }
            if (first.snapshots.size() != second.snapshots.size() ||
                first.history.size() != second.history.size())
            {
                diagnostic = "Live2D emotion replay sequence lengths diverged";
                result.diagnostic = diagnostic;
                return false;
            }
            for (std::size_t index = 0u; index < first.snapshots.size(); ++index)
            {
                if (!EqualSnapshot(first.snapshots[index], second.snapshots[index]))
                {
                    diagnostic = "Live2D emotion replay snapshots diverged";
                    result.diagnostic = diagnostic;
                    return false;
                }
            }
            for (std::size_t index = 0u; index < first.history.size(); ++index)
            {
                if (!EqualRecord(first.history[index], second.history[index]))
                {
                    diagnostic = "Live2D emotion replay transition history diverged";
                    result.diagnostic = diagnostic;
                    return false;
                }
            }
            result.passed = true;
            result.deterministic = true;
            result.diagnostic.clear();
            result.final_snapshot = first.snapshots.back();
            result.step_snapshots = std::move(first.snapshots);
            result.transition_history = std::move(first.history);
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Live2D emotion replay failed: ") + error.what();
            result = {};
            result.diagnostic = diagnostic;
            return false;
        }
    }
}
