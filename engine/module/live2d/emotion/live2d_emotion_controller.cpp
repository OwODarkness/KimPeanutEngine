#include "live2d_emotion_controller.h"

#include <utility>

namespace kpengine::live2d
{
    namespace
    {
        std::optional<Live2DEmotionPlan> DefaultDecision(
            const Live2DEmotionIntent &intent)
        {
            Live2DEmotionPlan plan;
            plan.name = intent.name;
            if (intent.name == "Normal")
            {
                plan.motion_candidates = {{"Idle", 0u, 0u}};
                plan.bubble_text = "o_o";
                return plan;
            }
            if (intent.name == "Happy")
            {
                plan.expression_candidates = {"exp_02", "exp_04"};
                plan.motion_candidates = {{"Idle", 1u, 0u}, {"Idle", 0u, 1u}};
                plan.bubble_text = "^_^";
                return plan;
            }
            if (intent.name == "Sad")
            {
                plan.expression_candidates = {"exp_03", "exp_01"};
                plan.motion_candidates = {{"FlickDown", 0u, 0u},
                                          {"Flick", 0u, 1u},
                                          {"Idle", 0u, 2u}};
                plan.bubble_text = "T_T";
                return plan;
            }
            if (intent.name == "Angry")
            {
                plan.expression_candidates = {"exp_08", "exp_07"};
                plan.motion_candidates = {{"Tap@Body", 0u, 0u},
                                          {"TapBody", 0u, 1u},
                                          {"Tap", 0u, 2u},
                                          {"Flick", 0u, 3u},
                                          {"Idle", 0u, 4u}};
                plan.bubble_text = ">_<";
                return plan;
            }
            return std::nullopt;
        }
    }

    Live2DEmotionController::Live2DEmotionController()
        : decision_(&DefaultDecision)
    {
    }

    Live2DEmotionController::Live2DEmotionController(Live2DEmotionDecision decision)
        : decision_(std::move(decision))
    {
        if (!decision_)
        {
            decision_ = &DefaultDecision;
        }
    }

    std::optional<Live2DEmotionPlan> Live2DEmotionController::Resolve(
        const std::string_view intent) const
    {
        return decision_(Live2DEmotionIntent{std::string(intent)});
    }

    Live2DEmotionOrchestrator::Live2DEmotionOrchestrator(
        Live2DEmotionDecision decision)
        : controller_(std::move(decision))
    {
    }

    bool Live2DEmotionOrchestrator::Apply(const std::string_view intent,
                                          const Live2DEmotionActions &actions)
    {
        diagnostic_.clear();
        const std::optional<Live2DEmotionPlan> plan = controller_.Resolve(intent);
        if (!plan.has_value())
        {
            diagnostic_ = "Unknown Live2D emotion intent: " + std::string(intent);
            return false;
        }

        std::string action_diagnostic;
        if (actions.clear_expression)
        {
            actions.clear_expression(action_diagnostic);
        }

        bool expression_applied = false;
        for (const std::string &expression : plan->expression_candidates)
        {
            if (actions.set_expression &&
                actions.set_expression(expression, action_diagnostic))
            {
                expression_applied = true;
                break;
            }
        }

        bool motion_applied = false;
        for (const Live2DEmotionMotionCandidate &candidate : plan->motion_candidates)
        {
            if (motion_applied || !actions.start_motion)
            {
                break;
            }
            motion_applied = actions.start_motion(candidate, action_diagnostic);
        }

        if (!expression_applied && !motion_applied)
        {
            diagnostic_ = action_diagnostic.empty()
                              ? "No authored expression or motion matched this preset"
                              : std::move(action_diagnostic);
            return false;
        }

        status_ = plan->name;
        if (!expression_applied)
        {
            status_ += " (body motion)";
        }

        if (actions.show_bubble &&
            !actions.show_bubble(plan->bubble_text, action_diagnostic))
        {
            diagnostic_ = std::move(action_diagnostic);
        }
        return true;
    }
}
