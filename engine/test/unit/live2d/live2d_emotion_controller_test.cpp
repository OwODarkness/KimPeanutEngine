#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "live2d_emotion_controller.h"

namespace kpengine::live2d
{
    TEST(Live2DEmotionControllerTest, AppliesExpressionAndMotionThroughCallbacks)
    {
        Live2DEmotionActions actions;
        std::vector<std::string> expressions;
        std::vector<std::string> motions;
        std::string bubble;
        actions.clear_expression = [](std::string &) {};
        actions.set_expression = [&expressions](const std::string_view name,
                                                 std::string &) {
            expressions.emplace_back(name);
            return name == "exp_02";
        };
        actions.start_motion = [&motions](const Live2DEmotionMotionCandidate &candidate,
                                          std::string &) {
            motions.push_back(candidate.group + ":" + std::to_string(candidate.index));
            return true;
        };
        actions.show_bubble = [&bubble](const std::string_view text, std::string &) {
            bubble = text;
            return true;
        };

        Live2DEmotionOrchestrator orchestrator;
        ASSERT_TRUE(orchestrator.Apply("Happy", actions));
        EXPECT_EQ(orchestrator.Status(), "Happy");
        EXPECT_TRUE(orchestrator.Diagnostic().empty());
        ASSERT_EQ(expressions.size(), 1u);
        EXPECT_EQ(expressions.front(), "exp_02");
        ASSERT_EQ(motions.size(), 1u);
        EXPECT_EQ(motions.front(), "Idle:1");
        EXPECT_EQ(bubble, "^_^");
    }

    TEST(Live2DEmotionControllerTest, ReportsUnknownIntentWithoutCallingActions)
    {
        bool called = false;
        Live2DEmotionActions actions;
        actions.clear_expression = [&called](std::string &) { called = true; };

        Live2DEmotionOrchestrator orchestrator;
        EXPECT_FALSE(orchestrator.Apply("Surprised", actions));
        EXPECT_FALSE(called);
        EXPECT_NE(orchestrator.Diagnostic().find("Unknown"), std::string::npos);
    }

    TEST(Live2DEmotionControllerTest, AcceptsAnInjectedDecisionProvider)
    {
        Live2DEmotionOrchestrator orchestrator([](const Live2DEmotionIntent &intent) {
            Live2DEmotionPlan plan;
            plan.name = intent.name;
            plan.motion_candidates = {{"Custom", 4u, 0u}};
            plan.bubble_text = ":-D";
            return std::optional<Live2DEmotionPlan>(std::move(plan));
        });

        std::string motion;
        Live2DEmotionActions actions;
        actions.start_motion = [&motion](const Live2DEmotionMotionCandidate &candidate,
                                         std::string &) {
            motion = candidate.group + ":" + std::to_string(candidate.index);
            return true;
        };

        ASSERT_TRUE(orchestrator.Apply("ModelDriven", actions));
        EXPECT_EQ(orchestrator.Status(), "ModelDriven (body motion)");
        EXPECT_EQ(motion, "Custom:4");
    }
}
