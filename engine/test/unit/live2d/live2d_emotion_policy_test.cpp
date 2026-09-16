#include <gtest/gtest.h>

#include <vector>

#include "live2d_emotion_policy.h"
#include "live2d_emotion_replay.h"
#include "live2d_emotion_resolver.h"

namespace kpengine::live2d
{
    namespace
    {
        Live2DProductData MakeProduct()
        {
            Live2DProductData product{};
            product.product_version = 2u;
            product.moc_bytes = {std::byte{0x01}};
            product.textures = {{"model.texture"}};
            product.motions = {
                {"Idle", 0u, false, 0.0, false, 0.0, {std::byte{0x02}}, false, {}},
                {"Joy", 1u, false, 0.0, false, 0.0, {std::byte{0x03}}, false, {}}};
            product.expressions = {{"Happy", {std::byte{0x04}}}};
            return product;
        }

        Live2DEmotionPolicy MakePolicy()
        {
            Live2DEmotionPolicy policy{};
            policy.fallback_state_id = "neutral_idle";
            policy.states = {
                {"neutral_idle", {"neutral", "idle"}, true},
                {"joy", {"joy", "open"}, false}};
            policy.bindings = {
                {"neutral_idle", Live2DPolicyMotionReference{"Idle", 0u}, std::nullopt},
                {"joy", Live2DPolicyMotionReference{"Joy", 1u},
                 std::optional<std::string>{"Happy"}}};
            return policy;
        }
    }

    TEST(Live2DEmotionPolicyTest, AcceptsVersionedBindingsToTypedProductData)
    {
        const Live2DProductData product = MakeProduct();
        const Live2DEmotionPolicy policy = MakePolicy();
        std::string diagnostic;

        ASSERT_TRUE(ValidateLive2DEmotionPolicy(policy, product, diagnostic))
            << diagnostic;
        ASSERT_NE(FindLive2DSemanticState(policy, "joy"), nullptr);
        ASSERT_NE(FindLive2DAuthoredBinding(policy, "joy"), nullptr);
        EXPECT_EQ(FindLive2DSemanticState(policy, "missing"), nullptr);
        EXPECT_EQ(FindLive2DAuthoredBinding(policy, "missing"), nullptr);
    }

    TEST(Live2DEmotionPolicyTest, RequiresIdleFallbackAndCompleteBindings)
    {
        const Live2DProductData product = MakeProduct();
        Live2DEmotionPolicy policy = MakePolicy();
        std::string diagnostic;

        policy.states[0].idle = false;
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, product, diagnostic));
        EXPECT_NE(diagnostic.find("fallback state must be idle"), std::string::npos);

        policy = MakePolicy();
        policy.bindings[1].motion.reset();
        policy.bindings[1].expression.reset();
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, product, diagnostic));
        EXPECT_NE(diagnostic.find("no authored behavior"), std::string::npos);
    }

    TEST(Live2DEmotionPolicyTest, RejectsMissingAuthoredReferencesAndOldProducts)
    {
        const Live2DProductData product = MakeProduct();
        Live2DEmotionPolicy policy = MakePolicy();
        std::string diagnostic;

        policy.bindings[1].motion->group = "Missing";
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, product, diagnostic));
        EXPECT_NE(diagnostic.find("missing motion"), std::string::npos);

        policy = MakePolicy();
        policy.bindings[1].expression = "Missing";
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, product, diagnostic));
        EXPECT_NE(diagnostic.find("missing expression"), std::string::npos);

        policy = MakePolicy();
        Live2DProductData v1 = product;
        v1.product_version = 1u;
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, v1, diagnostic));
        EXPECT_NE(diagnostic.find("Product V2"), std::string::npos);
    }

    TEST(Live2DEmotionPolicyTest, RejectsUnsupportedSchemaAndDuplicateStates)
    {
        const Live2DProductData product = MakeProduct();
        Live2DEmotionPolicy policy = MakePolicy();
        std::string diagnostic;

        policy.schema_version++;
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, product, diagnostic));
        EXPECT_NE(diagnostic.find("schema version"), std::string::npos);

        policy = MakePolicy();
        policy.states[1].id = policy.states[0].id;
        EXPECT_FALSE(ValidateLive2DEmotionPolicy(policy, product, diagnostic));
        EXPECT_NE(diagnostic.find("duplicate state"), std::string::npos);
    }

    TEST(Live2DEmotionResolverTest, TranslatesIntentAndAppliesPriorityRules)
    {
        const Live2DProductData product = MakeProduct();
        const auto policy = std::make_shared<const Live2DEmotionPolicy>(MakePolicy());
        std::string diagnostic;
        auto resolver = Live2DEmotionResolver::Create(policy, product, diagnostic);
        ASSERT_NE(resolver, nullptr) << diagnostic;

        Live2DBehaviorRequest request{};
        request.intent = {"joy", "open"};
        request.priority = 2;
        Live2DBehaviorResolution resolution{};
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        EXPECT_TRUE(resolution.accepted);
        EXPECT_TRUE(resolution.changed);
        EXPECT_EQ(resolution.reason, Live2DBehaviorTransitionReason::Initial);
        EXPECT_EQ(resolution.behavior.state_id, "joy");
        ASSERT_TRUE(resolution.behavior.motion.has_value());
        EXPECT_EQ(resolution.behavior.motion->group, "Joy");
        ASSERT_TRUE(resolution.behavior.expression.has_value());
        EXPECT_EQ(*resolution.behavior.expression, "Happy");
        EXPECT_EQ(resolution.transition_sequence, 1u);

        request.intent = {"neutral", "idle"};
        request.priority = 1;
        resolution = {};
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        EXPECT_FALSE(resolution.accepted);
        EXPECT_FALSE(resolution.changed);
        EXPECT_EQ(resolution.reason,
                  Live2DBehaviorTransitionReason::LowerPriorityRejected);
        EXPECT_EQ(resolver->CurrentStateId(), "joy");
        EXPECT_EQ(resolver->TransitionSequence(), 1u);

        request.force = true;
        request.blend_mode = Live2DBehaviorBlendMode::Immediate;
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        EXPECT_TRUE(resolution.accepted);
        EXPECT_TRUE(resolution.changed);
        EXPECT_EQ(resolution.reason, Live2DBehaviorTransitionReason::IntentChanged);
        EXPECT_EQ(resolution.blend_mode, Live2DBehaviorBlendMode::Immediate);
        EXPECT_EQ(resolver->CurrentStateId(), "neutral_idle");
    }

    TEST(Live2DEmotionResolverTest, UsesFallbackAndSupportsRetrigger)
    {
        const Live2DProductData product = MakeProduct();
        const auto policy = std::make_shared<const Live2DEmotionPolicy>(MakePolicy());
        std::string diagnostic;
        auto resolver = Live2DEmotionResolver::Create(policy, product, diagnostic);
        ASSERT_NE(resolver, nullptr) << diagnostic;

        Live2DBehaviorRequest request{};
        request.intent = {"unknown", "unknown"};
        Live2DBehaviorResolution resolution{};
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        EXPECT_TRUE(resolution.accepted);
        EXPECT_TRUE(resolution.changed);
        EXPECT_TRUE(resolution.used_fallback);
        EXPECT_EQ(resolution.reason, Live2DBehaviorTransitionReason::Fallback);
        EXPECT_EQ(resolution.behavior.state_id, "neutral_idle");

        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        EXPECT_TRUE(resolution.accepted);
        EXPECT_FALSE(resolution.changed);
        EXPECT_EQ(resolution.reason, Live2DBehaviorTransitionReason::SameStateIgnored);
        EXPECT_EQ(resolution.transition_sequence, 1u);

        request.retrigger = true;
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        EXPECT_TRUE(resolution.changed);
        EXPECT_EQ(resolution.reason, Live2DBehaviorTransitionReason::Retriggered);
        EXPECT_EQ(resolution.transition_sequence, 2u);
    }

    TEST(Live2DEmotionResolverTest, IdenticalRequestsProduceIdenticalTransitions)
    {
        const Live2DProductData product = MakeProduct();
        const auto policy = std::make_shared<const Live2DEmotionPolicy>(MakePolicy());
        std::string first_diagnostic;
        std::string second_diagnostic;
        auto first = Live2DEmotionResolver::Create(policy, product, first_diagnostic);
        auto second = Live2DEmotionResolver::Create(policy, product, second_diagnostic);
        ASSERT_NE(first, nullptr) << first_diagnostic;
        ASSERT_NE(second, nullptr) << second_diagnostic;

        const std::vector<Live2DBehaviorRequest> requests = {
            {{"joy", "open"}, 2, false, false,
             Live2DBehaviorBlendMode::AuthoredFadeOut},
            {{"neutral", "idle"}, 1, false, false,
             Live2DBehaviorBlendMode::AuthoredFadeOut},
            {{"neutral", "idle"}, 3, true, true,
             Live2DBehaviorBlendMode::Immediate}};
        for (const Live2DBehaviorRequest &request : requests)
        {
            Live2DBehaviorResolution first_resolution{};
            Live2DBehaviorResolution second_resolution{};
            ASSERT_TRUE(first->Resolve(request, first_resolution, first_diagnostic))
                << first_diagnostic;
            ASSERT_TRUE(second->Resolve(request, second_resolution, second_diagnostic))
                << second_diagnostic;
            EXPECT_EQ(first_resolution.accepted, second_resolution.accepted);
            EXPECT_EQ(first_resolution.changed, second_resolution.changed);
            EXPECT_EQ(first_resolution.used_fallback, second_resolution.used_fallback);
            EXPECT_EQ(first_resolution.transition_sequence,
                      second_resolution.transition_sequence);
            EXPECT_EQ(first_resolution.reason, second_resolution.reason);
            EXPECT_EQ(first_resolution.behavior.state_id,
                      second_resolution.behavior.state_id);
        }
    }

    TEST(Live2DEmotionResolverTest, ExposesSnapshotAndTerminalResults)
    {
        const Live2DProductData product = MakeProduct();
        const auto policy = std::make_shared<const Live2DEmotionPolicy>(MakePolicy());
        std::string diagnostic;
        auto resolver = Live2DEmotionResolver::Create(policy, product, diagnostic);
        ASSERT_NE(resolver, nullptr) << diagnostic;

        Live2DBehaviorRequest request{};
        request.intent = {"joy", "open"};
        request.priority = 2;
        Live2DBehaviorResolution resolution{};
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;

        Live2DBehaviorSnapshot snapshot = resolver->Snapshot();
        EXPECT_TRUE(snapshot.active);
        EXPECT_EQ(snapshot.state_id, "joy");
        EXPECT_EQ(snapshot.behavior.state_id, "joy");
        ASSERT_TRUE(snapshot.behavior.motion.has_value());
        EXPECT_EQ(snapshot.behavior.motion->group, "Joy");
        ASSERT_TRUE(snapshot.behavior.expression.has_value());
        EXPECT_EQ(*snapshot.behavior.expression, "Happy");
        EXPECT_EQ(snapshot.terminal_state,
                  Live2DBehaviorTerminalState::Pending);

        auto history = resolver->TransitionHistory();
        ASSERT_EQ(history.size(), 1u);
        EXPECT_EQ(history[0].reason, Live2DBehaviorTransitionReason::Initial);
        EXPECT_EQ(history[0].terminal_state,
                  Live2DBehaviorTerminalState::Pending);

        ASSERT_TRUE(resolver->CompleteActive(diagnostic)) << diagnostic;
        snapshot = resolver->Snapshot();
        EXPECT_FALSE(snapshot.active);
        EXPECT_EQ(snapshot.terminal_state,
                  Live2DBehaviorTerminalState::Completed);
        history = resolver->TransitionHistory();
        ASSERT_EQ(history.size(), 1u);
        EXPECT_EQ(history[0].terminal_state,
                  Live2DBehaviorTerminalState::Completed);
        EXPECT_FALSE(resolver->CompleteActive(diagnostic));
        EXPECT_NE(diagnostic.find("no active behavior"), std::string::npos);

        request.intent = {"neutral", "idle"};
        request.force = true;
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        ASSERT_TRUE(resolver->CancelActive(diagnostic)) << diagnostic;
        snapshot = resolver->Snapshot();
        EXPECT_FALSE(snapshot.active);
        EXPECT_EQ(snapshot.terminal_state,
                  Live2DBehaviorTerminalState::Cancelled);
        history = resolver->TransitionHistory();
        ASSERT_EQ(history.size(), 2u);
        EXPECT_EQ(history.back().terminal_state,
                  Live2DBehaviorTerminalState::Cancelled);
    }

    TEST(Live2DEmotionResolverTest, InterruptsActiveBehaviorAndBoundsHistory)
    {
        const Live2DProductData product = MakeProduct();
        const auto policy = std::make_shared<const Live2DEmotionPolicy>(MakePolicy());
        std::string diagnostic;
        auto resolver = Live2DEmotionResolver::Create(policy, product, diagnostic);
        ASSERT_NE(resolver, nullptr) << diagnostic;

        Live2DBehaviorRequest request{};
        request.intent = {"joy", "open"};
        request.priority = 2;
        Live2DBehaviorResolution resolution{};
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;

        request.intent = {"neutral", "idle"};
        request.force = true;
        ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic)) << diagnostic;
        auto history = resolver->TransitionHistory();
        ASSERT_EQ(history.size(), 2u);
        EXPECT_EQ(history[0].terminal_state,
                  Live2DBehaviorTerminalState::Interrupted);
        EXPECT_EQ(history[1].terminal_state,
                  Live2DBehaviorTerminalState::Pending);

        for (std::size_t index = 0u;
             index < kLive2DEmotionTransitionHistoryCapacity + 5u; ++index)
        {
            request.intent = index % 2u == 0u
                                 ? Live2DBehaviorIntent{"joy", "open"}
                                 : Live2DBehaviorIntent{"neutral", "idle"};
            request.retrigger = true;
            ASSERT_TRUE(resolver->Resolve(request, resolution, diagnostic))
                << diagnostic;
        }

        history = resolver->TransitionHistory();
        EXPECT_EQ(history.size(), kLive2DEmotionTransitionHistoryCapacity);
        ASSERT_FALSE(history.empty());
        for (std::size_t index = 1u; index < history.size(); ++index)
        {
            EXPECT_LT(history[index - 1u].record_sequence,
                      history[index].record_sequence);
        }
        EXPECT_EQ(history.back().transition_sequence,
                  resolver->TransitionSequence());
    }

    TEST(Live2DEmotionReplayTest, CoversFallbackTransitionInterruptionAndRecovery)
    {
        const Live2DProductData product = MakeProduct();
        Live2DEmotionReplayResult result{};
        std::string diagnostic;

        ASSERT_TRUE(RunLive2DEmotionReplay(product, result, diagnostic))
            << diagnostic;
        EXPECT_TRUE(result.passed);
        EXPECT_TRUE(result.deterministic);
        ASSERT_EQ(result.step_snapshots.size(), 4u);
        EXPECT_EQ(result.step_snapshots[0].state_id, "neutral_idle");
        EXPECT_EQ(result.step_snapshots[1].state_id, "joy_open");
        EXPECT_EQ(result.step_snapshots[2].state_id, "neutral_recovery");
        EXPECT_EQ(result.step_snapshots[3].terminal_state,
                  Live2DBehaviorTerminalState::Completed);
        ASSERT_EQ(result.transition_history.size(), 3u);
        EXPECT_EQ(result.transition_history[0].terminal_state,
                  Live2DBehaviorTerminalState::Interrupted);
        EXPECT_EQ(result.transition_history[1].terminal_state,
                  Live2DBehaviorTerminalState::Interrupted);
        EXPECT_EQ(result.transition_history[2].terminal_state,
                  Live2DBehaviorTerminalState::Completed);
    }

    TEST(Live2DEmotionReplayTest, RequiresTypedMotionProduct)
    {
        Live2DProductData product = MakeProduct();
        product.product_version = 1u;
        Live2DEmotionReplayResult result{};
        std::string diagnostic;

        EXPECT_FALSE(RunLive2DEmotionReplay(product, result, diagnostic));
        EXPECT_NE(diagnostic.find("Product V2"), std::string::npos);
        EXPECT_FALSE(result.passed);
    }
}
