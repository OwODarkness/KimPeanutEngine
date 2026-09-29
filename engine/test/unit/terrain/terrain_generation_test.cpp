#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

#include "evaluation/terrain_generation.h"

namespace
{
    using namespace kpengine::terrain;

    GridDomain2D FixtureDomain()
    {
        return {5, 3, -2.0, 7.0, 0.5, 2.0, 10.0};
    }

    TerrainRecipe ConstantRecipe(float value = 2.0f)
    {
        TerrainRecipe recipe;
        recipe.seed = 1234;
        recipe.domain = FixtureDomain();
        recipe.nodes.push_back({"source", "terrain.scalar.constant", 1,
                                {{"value", value}}, {}});
        return recipe;
    }

    std::shared_ptr<OperatorRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<OperatorRegistry>();
        std::string diagnostic;
        EXPECT_TRUE(registry->RegisterBuiltins(diagnostic)) << diagnostic;
        return registry;
    }

    void RegisterOffset(const std::shared_ptr<OperatorRegistry> &registry,
                        std::atomic_int *calls = nullptr)
    {
        std::string diagnostic;
        ASSERT_TRUE(registry->Register({"test.scalar.offset", 1,
            {{"source", PortType::ScalarField2D}}, {{"value", PortType::ScalarField2D}},
            [calls](const OperatorContext &context, const nlohmann::json &parameters,
                    const OperatorInputs &inputs) {
                if (calls) ++*calls;
                const float amount = parameters.at("amount").get<float>();
                auto samples = inputs.at("source")->Samples();
                for (auto &sample : samples) sample += amount;
                std::string error;
                auto output = ScalarField2D::Create(context.domain, std::move(samples),
                    context.maximum_samples, error);
                if (!output) throw std::runtime_error(error);
                return OperatorOutputs{{"value", std::move(output)}};
            }}, diagnostic)) << diagnostic;
    }

    TEST(TerrainCoreTest, RectangularDomainUsesExplicitMeterSpacing)
    {
        std::string diagnostic;
        std::vector<float> samples(15);
        for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<float>(i);
        auto field = ScalarField2D::Create(FixtureDomain(), std::move(samples), 15, diagnostic);
        ASSERT_TRUE(field) << diagnostic;
        EXPECT_EQ(field->Domain().width, 5u);
        EXPECT_EQ(field->Domain().height, 3u);
        EXPECT_DOUBLE_EQ(field->Domain().origin_x_m + 4 * field->Domain().spacing_x_m, 0.0);
        EXPECT_DOUBLE_EQ(field->Domain().origin_z_m + 2 * field->Domain().spacing_z_m, 11.0);
        EXPECT_FLOAT_EQ(field->At(4, 2), 14.0f);
        EXPECT_THROW(field->At(5, 2), std::out_of_range);
    }

    TEST(TerrainCoreTest, RejectsInvalidDimensionsSpacingBudgetAndSamples)
    {
        auto domain = FixtureDomain();
        domain.width = 1;
        EXPECT_THROW(domain.SampleCount(100), std::invalid_argument);
        domain = FixtureDomain();
        domain.spacing_z_m = 0.0;
        EXPECT_THROW(domain.SampleCount(100), std::invalid_argument);
        domain = FixtureDomain();
        domain.spacing_x_m = std::numeric_limits<double>::max();
        EXPECT_THROW(domain.SampleCount(100), std::invalid_argument);
        domain = FixtureDomain();
        EXPECT_THROW(domain.SampleCount(14), std::length_error);

        std::string diagnostic;
        EXPECT_FALSE(ScalarField2D::Create(FixtureDomain(), std::vector<float>(14), 100, diagnostic));
        std::vector<float> samples(15, 0.0f);
        samples[2] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(ScalarField2D::Create(FixtureDomain(), std::move(samples), 100, diagnostic));
    }

    TEST(TerrainRecipeTest, JsonRoundTripIsCanonicalAndValidatesDomain)
    {
        auto recipe = ConstantRecipe();
        const auto encoded = recipe.ToJson();
        const auto decoded = TerrainRecipe::FromJson(encoded);
        EXPECT_EQ(decoded.ToJson(), encoded);
        auto malformed = encoded;
        malformed["domain"]["spacing_x_m"] = -1.0;
        EXPECT_THROW(TerrainRecipe::FromJson(malformed), std::invalid_argument);
    }

    TEST(TerrainEvaluationTest, IndependentOperatorRegistersAndOnlyDependentNodesReevaluate)
    {
        auto registry = MakeRegistry();
        std::atomic_int offset_calls = 0;
        RegisterOffset(registry, &offset_calls);
        TerrainEvaluator evaluator(registry);
        auto recipe = ConstantRecipe();
        recipe.nodes.push_back({"offset", "test.scalar.offset", 1, {{"amount", 3.0f}},
                                {{"source", {"source", "value"}}}});
        recipe.nodes.push_back({"independent", "terrain.scalar.constant", 1,
                                {{"value", 9.0f}}, {}});
        const auto first = evaluator.Evaluate(recipe);
        ASSERT_TRUE(first.succeeded) << first.diagnostic;
        EXPECT_FLOAT_EQ(first.nodes.at("offset").outputs.at("value")->At(3, 1), 5.0f);
        ASSERT_EQ(offset_calls, 1);

        recipe.nodes[2].parameters["value"] = 10.0f;
        const auto second = evaluator.Evaluate(recipe);
        ASSERT_TRUE(second.succeeded) << second.diagnostic;
        EXPECT_TRUE(second.nodes.at("source").cache_hit);
        EXPECT_TRUE(second.nodes.at("offset").cache_hit);
        EXPECT_FALSE(second.nodes.at("independent").cache_hit);
        EXPECT_EQ(offset_calls, 1);
    }

    TEST(TerrainEvaluationTest, RejectsCyclesMissingAndIncompatiblePortsBeforeExecution)
    {
        auto registry = MakeRegistry();
        RegisterOffset(registry);
        std::string diagnostic;
        ASSERT_TRUE(registry->Register({"test.heightfield.source", 1, {},
            {{"value", PortType::Heightfield}}, [](const OperatorContext &, const nlohmann::json &,
                const OperatorInputs &) { return OperatorOutputs{}; }}, diagnostic)) << diagnostic;
        TerrainEvaluator evaluator(registry);

        auto cycle = ConstantRecipe();
        cycle.nodes.clear();
        cycle.nodes.push_back({"a", "test.scalar.offset", 1, {{"amount", 1.0f}},
                               {{"source", {"b", "value"}}}});
        cycle.nodes.push_back({"b", "test.scalar.offset", 1, {{"amount", 1.0f}},
                               {{"source", {"a", "value"}}}});
        auto result = evaluator.Evaluate(cycle);
        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("cycle"), std::string::npos);

        auto incompatible = ConstantRecipe();
        incompatible.nodes.insert(incompatible.nodes.begin(), {"height", "test.heightfield.source", 1,
            nlohmann::json::object(), {}});
        incompatible.nodes.push_back({"consumer", "test.scalar.offset", 1, {{"amount", 1.0f}},
                                      {{"source", {"height", "value"}}}});
        result = evaluator.Evaluate(incompatible);
        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("incompatible"), std::string::npos);

        auto missing = ConstantRecipe();
        missing.nodes.push_back({"consumer", "test.scalar.offset", 1, {{"amount", 1.0f}},
                                 {{"source", {"absent", "value"}}}});
        result = evaluator.Evaluate(missing);
        EXPECT_FALSE(result.succeeded);
    }

    TEST(TerrainEvaluationTest, SameProfileIsRepeatableAndCancellationIsObserved)
    {
        TerrainEvaluator evaluator(MakeRegistry());
        const auto recipe = ConstantRecipe(-0.25f);
        const auto first = evaluator.Evaluate(recipe);
        const auto second = evaluator.Evaluate(recipe);
        ASSERT_TRUE(first.succeeded);
        ASSERT_TRUE(second.succeeded);
        EXPECT_EQ(first.nodes.at("source").content_hash, second.nodes.at("source").content_hash);
        EXPECT_EQ(first.nodes.at("source").outputs.at("value")->Samples(),
                  second.nodes.at("source").outputs.at("value")->Samples());

        std::atomic_bool cancelled{true};
        const auto stopped = evaluator.Evaluate(recipe, &cancelled);
        EXPECT_FALSE(stopped.succeeded);
        EXPECT_TRUE(stopped.cancelled);
    }

    TEST(TerrainEvaluationTest, EnforcesNodeAndRetainedResultBudgets)
    {
        auto registry = MakeRegistry();
        EvaluationOptions options;
        options.maximum_nodes = 1;
        TerrainEvaluator node_limited(registry, options);
        auto recipe = ConstantRecipe();
        recipe.nodes.push_back({"second", "terrain.scalar.constant", 1,
                                {{"value", 3.0f}}, {}});
        auto result = node_limited.Evaluate(recipe);
        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("node budget"), std::string::npos);

        options.maximum_nodes = 4;
        options.maximum_result_bytes = 15 * sizeof(float) - 1;
        TerrainEvaluator byte_limited(registry, options);
        result = byte_limited.Evaluate(ConstantRecipe());
        EXPECT_FALSE(result.succeeded);
        EXPECT_NE(result.diagnostic.find("memory budget"), std::string::npos);
    }

    std::vector<float> EvaluateThroughExecutor(std::size_t worker_count)
    {
        auto registry = MakeRegistry();
        GenerationExecutor executor(registry, worker_count, 2, 2);
        if (!executor.Submit(1, ConstantRecipe(4.5f))) return {};
        GenerationJobResult result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!executor.TryPop(result) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_EQ(result.revision, 1u);
        EXPECT_TRUE(result.evaluation.succeeded) << result.evaluation.diagnostic;
        auto values = result.evaluation.nodes.at("source").outputs.at("value")->Samples();
        executor.Shutdown();
        return values;
    }

    TEST(TerrainExecutorTest, WorkerCountDoesNotChangeResultsAndRevisionCancellationDropsStaleWork)
    {
        EXPECT_EQ(EvaluateThroughExecutor(1), EvaluateThroughExecutor(3));
        auto registry = MakeRegistry();
        GenerationExecutor executor(registry, 2, 2, 1);
        ASSERT_TRUE(executor.Submit(1, ConstantRecipe(1.0f)));
        executor.CancelBefore(2);
        ASSERT_TRUE(executor.Submit(2, ConstantRecipe(8.0f)));
        GenerationJobResult result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!executor.TryPop(result) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_EQ(result.revision, 2u);
        ASSERT_TRUE(result.evaluation.succeeded) << result.evaluation.diagnostic;
        EXPECT_FLOAT_EQ(result.evaluation.nodes.at("source").outputs.at("value")->At(0, 0), 8.0f);
        executor.Shutdown();
    }
}
