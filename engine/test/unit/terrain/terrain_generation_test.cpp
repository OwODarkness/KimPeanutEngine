#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
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

    TEST(TerrainDerivedFieldTest, PlaneSlopeUsesWorldSpacingAndCurvatureIsZeroAtEdges)
    {
        const GridDomain2D domain{5, 4, -2.0, 7.0, 0.5, 2.0, 0.0};
        std::vector<float> samples;
        for (std::uint32_t y = 0; y < domain.height; ++y)
            for (std::uint32_t x = 0; x < domain.width; ++x)
                samples.push_back(static_cast<float>(2.0 * (domain.origin_x_m + x * domain.spacing_x_m) -
                                                     0.25 * (domain.origin_z_m + y * domain.spacing_z_m)));
        std::string diagnostic;
        auto field = ScalarField2D::Create(domain, std::move(samples), 100, diagnostic);
        ASSERT_TRUE(field) << diagnostic;
        const auto slope = ComputeSlopeRadians(*field);
        const float expected = static_cast<float>(std::atan(std::hypot(2.0, -0.25)));
        for (float value : slope) EXPECT_NEAR(value, expected, 1e-6f);
        for (float value : ComputeCurvaturePerMeter(*field)) EXPECT_NEAR(value, 0.0f, 1e-6f);
    }

    TEST(TerrainMeshTest, CoarseAndFineSamplingShareWorldPositionsAndGlobalNormals)
    {
        const auto make_plane = [](GridDomain2D domain) {
            std::vector<float> samples;
            for (std::uint32_t y = 0; y < domain.height; ++y)
                for (std::uint32_t x = 0; x < domain.width; ++x)
                    samples.push_back(static_cast<float>(3.0 + 0.5 * (domain.origin_x_m + x * domain.spacing_x_m) -
                                                         0.25 * (domain.origin_z_m + y * domain.spacing_z_m)));
            std::string diagnostic;
            auto result = ScalarField2D::Create(domain, std::move(samples), 100, diagnostic);
            EXPECT_TRUE(result) << diagnostic;
            return result;
        };
        const auto fine = BuildHeightfieldMesh(*make_plane({5, 5, -2.0, -2.0, 1.0, 1.0, 10.0}));
        const auto coarse = BuildHeightfieldMesh(*make_plane({3, 3, -2.0, -2.0, 2.0, 2.0, 10.0}));
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 3; ++x)
            {
                const auto &a = fine.vertices[static_cast<std::size_t>(y * 2) * 5 + x * 2];
                const auto &b = coarse.vertices[static_cast<std::size_t>(y) * 3 + x];
                EXPECT_EQ(a.position, b.position);
                EXPECT_EQ(a.normal, b.normal);
            }
        EXPECT_EQ(coarse.indices.size(), 24u);
    }

    TEST(TerrainProjectionTest, VerticalMeshProjectionSamplesTopmostSurfaceAndUsesExplicitFallback)
    {
        kpengine::data::MeshData mesh;
        const auto add = [&mesh](float x, float y, float z) {
            kpengine::data::Vertex vertex{};
            vertex.position = kpengine::Vector3f{x, y, z};
            mesh.vertices.push_back(vertex);
        };
        add(0.0f, 0.0f, 0.0f);
        add(2.0f, 1.0f, 0.0f);
        add(2.0f, 5.0f, 2.0f);
        add(0.0f, 4.0f, 2.0f);
        mesh.indices = {0, 1, 2, 0, 2, 3};
        const GridDomain2D domain{3, 3, 0.0, 0.0, 1.0, 1.0, 10.0};
        std::string diagnostic;
        auto projected = ProjectMeshToHeightfield(mesh, domain, -100.0f, 20, diagnostic);
        ASSERT_TRUE(projected) << diagnostic;
        EXPECT_FLOAT_EQ(projected->At(1, 1), -7.5f);
        EXPECT_FLOAT_EQ(projected->At(2, 2), -5.0f);
        EXPECT_FLOAT_EQ(BuildHeightfieldMesh(*projected).vertices[8].position.y_, 5.0f);
        const std::size_t original_vertex_count = mesh.vertices.size();
        for (std::size_t i = 0; i < original_vertex_count; ++i)
        {
            auto vertex = mesh.vertices[i];
            vertex.position.y_ += 10.0f;
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.insert(mesh.indices.end(), {4, 5, 6, 4, 6, 7});
        const auto topmost = ProjectMeshToHeightfield(mesh, domain, -100.0f, 20, diagnostic);
        ASSERT_TRUE(topmost) << diagnostic;
        EXPECT_FLOAT_EQ(topmost->At(1, 1), 2.5f);
        const auto empty = ProjectMeshToHeightfield({}, domain, -100.0f, 20, diagnostic);
        ASSERT_TRUE(empty) << diagnostic;
        EXPECT_FLOAT_EQ(empty->At(1, 1), -100.0f);
    }

    TEST(TerrainDrainageTest, PriorityFloodProducesAcyclicRoutesAndConservesCellAccumulation)
    {
        const GridDomain2D domain{7, 6, 0.0, 0.0, 2.0, 0.5, 0.0};
        std::vector<float> samples(domain.SampleCount(100), 10.0f);
        for (std::uint32_t y = 1; y + 1 < domain.height; ++y)
            for (std::uint32_t x = 1; x + 1 < domain.width; ++x)
                samples[static_cast<std::size_t>(y) * domain.width + x] = 0.0f;
        std::string diagnostic;
        auto field = ScalarField2D::Create(domain, std::move(samples), 100, diagnostic);
        ASSERT_TRUE(field) << diagnostic;
        const auto routing = RouteDrainage(*field, DrainageOutletPolicy::Perimeter);
        ASSERT_EQ(routing.flood_order.size(), field->Samples().size());
        for (std::uint32_t index = 0; index < routing.downstream.size(); ++index)
        {
            auto current = index;
            std::size_t steps = 0;
            while (routing.downstream[current] != DrainageNetwork::NoDownstream &&
                   steps <= routing.downstream.size())
            {
                current = routing.downstream[current];
                ++steps;
            }
            EXPECT_LT(steps, routing.downstream.size());
            if (routing.downstream[index] == DrainageNetwork::NoDownstream)
            {
                const auto x = index % domain.width;
                const auto y = index / domain.width;
                EXPECT_TRUE(x == 0 || y == 0 || x + 1 == domain.width || y + 1 == domain.height);
            }
        }
        EXPECT_FLOAT_EQ(routing.filled_elevation_m[3 * domain.width + 3], 10.0f);
        double outlet_total = 0.0;
        for (std::size_t i = 0; i < routing.downstream.size(); ++i)
            if (routing.downstream[i] == DrainageNetwork::NoDownstream)
                outlet_total += routing.accumulation_cells[i];
        EXPECT_DOUBLE_EQ(outlet_total, static_cast<double>(field->Samples().size()));

        std::vector<std::uint8_t> lake_mask(field->Samples().size(), 0);
        lake_mask[3 * domain.width + 3] = 1;
        const auto lake_routing = RouteDrainage(*field,
            DrainageOutletPolicy::AuthoredLakesAndPerimeter, lake_mask);
        EXPECT_EQ(lake_routing.downstream[3 * domain.width + 3], DrainageNetwork::NoDownstream);
        EXPECT_THROW(RouteDrainage(*field, DrainageOutletPolicy::AuthoredLakesAndPerimeter, {}),
                     std::invalid_argument);
    }

    TEST(TerrainOperatorsTest, RasterResamplesInWorldCoordinatesAndNoiseOperatorsRepeat)
    {
        auto registry = MakeRegistry();
        TerrainEvaluator evaluator(registry);
        TerrainRecipe recipe;
        recipe.seed = 41;
        recipe.domain = {3, 3, 0.0, 0.0, 1.0, 1.0, 0.0};
        recipe.nodes.push_back({"raster", "terrain.heightfield.raster", 1,
            {{"width", 2}, {"height", 2}, {"origin_x_m", 0.0}, {"origin_z_m", 0.0},
             {"spacing_x_m", 2.0}, {"spacing_z_m", 2.0}, {"samples_m", {0.0, 2.0, 4.0, 6.0}}}, {}});
        recipe.nodes.push_back({"detail", "terrain.heightfield.ridged_detail", 1,
            {{"amplitude_m", 2.0}, {"frequency_per_m", 0.125}, {"octaves", 4}},
            {{"source", {"raster", "height"}}}});
        const auto first = evaluator.Evaluate(recipe);
        ASSERT_TRUE(first.succeeded) << first.diagnostic;
        EXPECT_FLOAT_EQ(first.nodes.at("raster").outputs.at("height")->At(1, 1), 3.0f);
        const auto first_samples = first.nodes.at("detail").outputs.at("height")->Samples();
        evaluator.ClearCache();
        const auto second = evaluator.Evaluate(recipe);
        ASSERT_TRUE(second.succeeded) << second.diagnostic;
        EXPECT_EQ(first_samples, second.nodes.at("detail").outputs.at("height")->Samples());
    }

    TEST(TerrainFixturesTest, PlateauMountainBasinAndCoastalPlainHaveExpectedSamples)
    {
        const auto load = [](const char *name) {
            std::ifstream stream(std::string(KPENGINE_TERRAIN_FIXTURE_DIR) + "/" + name);
            if (!stream) throw std::runtime_error("terrain fixture could not be opened");
            nlohmann::json json;
            stream >> json;
            return TerrainRecipe::FromJson(json);
        };
        TerrainEvaluator evaluator(MakeRegistry());
        auto plateau = evaluator.Evaluate(load("plateau.terrainrecipe.json"));
        ASSERT_TRUE(plateau.succeeded) << plateau.diagnostic;
        EXPECT_FLOAT_EQ(plateau.nodes.at("plateau").outputs.at("height")->At(2, 2), 100.0f);

        auto basin = evaluator.Evaluate(load("mountain_basin.terrainrecipe.json"));
        ASSERT_TRUE(basin.succeeded) << basin.diagnostic;
        EXPECT_NEAR(basin.nodes.at("mountain_ring").outputs.at("height")->At(4, 4),
                    120.0 * std::exp(-0.5 * 4.0 / (0.75 * 0.75)), 1e-5);
        EXPECT_NEAR(basin.nodes.at("mountain_ring").outputs.at("height")->At(2, 4), 120.0f, 1e-4f);

        auto coastal = evaluator.Evaluate(load("coastal_plain.terrainrecipe.json"));
        ASSERT_TRUE(coastal.succeeded) << coastal.diagnostic;
        EXPECT_NEAR(coastal.nodes.at("coast_reference").outputs.at("height")->At(2, 2), -4.0f, 1e-6f);
        EXPECT_TRUE(std::isfinite(coastal.nodes.at("coastal_detail").outputs.at("height")->At(2, 2)));
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

    TEST(TerrainEvaluationTest, CachedOutputsCountTowardRetainedResultBudget)
    {
        auto registry = MakeRegistry();
        EvaluationOptions options;
        options.maximum_result_bytes = FixtureDomain().SampleCount(100) * sizeof(float);
        options.maximum_cache_bytes = options.maximum_result_bytes * 4;
        TerrainEvaluator evaluator(registry, options);
        auto recipe = ConstantRecipe();
        recipe.nodes.front().id = "a_source";

        const auto cached = evaluator.Evaluate(recipe);
        ASSERT_TRUE(cached.succeeded) << cached.diagnostic;
        recipe.nodes.push_back({"independent", "terrain.scalar.constant", 1,
                                {{"value", 3.0f}}, {}});

        const auto over_budget = evaluator.Evaluate(recipe);
        EXPECT_FALSE(over_budget.succeeded);
        EXPECT_NE(over_budget.diagnostic.find("memory budget"), std::string::npos);
        ASSERT_TRUE(over_budget.nodes.contains("a_source"));
        EXPECT_TRUE(over_budget.nodes.at("a_source").cache_hit);
    }

    TEST(TerrainHydrologyTest, OpenBoundaryIsInvariantToWorldDatumTranslation)
    {
        auto registry = MakeRegistry();
        std::string diagnostic;
        ASSERT_TRUE(registry->Register({"test.hydraulic_open_boundary_inputs", 1, {},
            {{"state", PortType::LayeredHeightfield2D},
             {"rain", PortType::ScalarField2D},
             {"erodibility", PortType::ScalarField2D},
             {"hardness", PortType::ScalarField2D},
             {"obstacle", PortType::ScalarField2D}},
            [](const OperatorContext &context, const nlohmann::json &,
               const OperatorInputs &) {
                const std::size_t count = context.domain.SampleCount(context.maximum_samples);
                std::vector<float> bedrock(count, 0.0f), empty(count, 0.0f),
                    water(count, 0.25f);
                std::string error;
                auto state = LayeredHeightfield2D::Create(context.domain, bedrock,
                    empty, empty, water, empty, context.maximum_samples, error);
                if (!state) throw std::runtime_error(error);
                auto field = [&](const float value) {
                    std::string field_error;
                    auto result = ScalarField2D::Create(context.domain,
                        std::vector<float>(count, value), context.maximum_samples,
                        field_error);
                    if (!result) throw std::runtime_error(field_error);
                    return result;
                };
                return OperatorOutputs{{"state", std::move(state)},
                    {"rain", field(0.0f)}, {"erodibility", field(1.0f)},
                    {"hardness", field(0.0f)}, {"obstacle", field(0.0f)}};
            }}, diagnostic)) << diagnostic;

        TerrainEvaluator evaluator(registry);
        const auto evaluate_at_datum = [&](const double datum) {
            TerrainRecipe recipe;
            recipe.domain = {3, 3, 0.0, 0.0, 1.0, 1.0, datum};
            recipe.nodes.push_back({"inputs", "test.hydraulic_open_boundary_inputs", 1,
                nlohmann::json::object(), {}});
            recipe.nodes.push_back({"hydraulic", "terrain.erosion.hydraulic_pipe", 1,
                {{"duration_s", 0.05}, {"maximum_timestep_s", 0.05},
                 {"capacity_kg_s_per_m3", 0.0}, {"boundary", "open"}},
                {{"state", {"inputs", "state"}},
                 {"rain_rate_m_per_s", {"inputs", "rain"}},
                 {"erodibility_0_1", {"inputs", "erodibility"}},
                 {"hardness_0_1", {"inputs", "hardness"}},
                 {"obstacle_0_1", {"inputs", "obstacle"}}}});
            recipe.nodes.push_back({"hydraulic_compact", "terrain.erosion.hydraulic_pipe", 2,
                {{"duration_s", 0.05}, {"maximum_timestep_s", 0.05},
                 {"capacity_kg_s_per_m3", 0.0}, {"boundary", "open"}},
                {{"state", {"inputs", "state"}},
                 {"rain_rate_m_per_s", {"inputs", "rain"}},
                 {"erodibility_0_1", {"inputs", "erodibility"}},
                 {"hardness_0_1", {"inputs", "hardness"}},
                 {"obstacle_0_1", {"inputs", "obstacle"}}}});
            return evaluator.Evaluate(recipe);
        };

        const auto local = evaluate_at_datum(0.0);
        const auto translated = evaluate_at_datum(1000.0);
        ASSERT_TRUE(local.succeeded) << local.diagnostic;
        ASSERT_TRUE(translated.succeeded) << translated.diagnostic;
        const auto &local_result = local.nodes.at("hydraulic");
        const auto &translated_result = translated.nodes.at("hydraulic");
        const auto &local_export = local_result.outputs.at("water_exported_m3")->Samples();
        const auto &translated_export =
            translated_result.outputs.at("water_exported_m3")->Samples();
        const float local_total = std::accumulate(local_export.begin(), local_export.end(), 0.0f);
        const float translated_total = std::accumulate(
            translated_export.begin(), translated_export.end(), 0.0f);
        EXPECT_GT(local_total, 0.0f);
        EXPECT_FLOAT_EQ(translated_total, local_total);
        EXPECT_EQ(translated_result.outputs.at("water_depth_m")->Samples(),
                  local_result.outputs.at("water_depth_m")->Samples());
        const auto &compact = translated.nodes.at("hydraulic_compact");
        EXPECT_FALSE(compact.outputs.contains("substep_count"));
        EXPECT_TRUE(compact.scalar_metadata.contains("substep_count"));
        EXPECT_GT(compact.scalar_metadata.at("substep_count"), 0.0);
        EXPECT_LE(std::abs(compact.scalar_metadata.at("water_budget_relative_residual")),
                  2.0e-6);
    }

    TEST(TerrainHydrologyTest, ComparesThermalRelaxationAndVirtualPipeOnSameTerrainFixture)
    {
        auto registry = MakeRegistry();
#ifndef KPENGINE_TERRAIN_FIXTURE_DIR
#define KPENGINE_TERRAIN_FIXTURE_DIR ""
#endif
        std::ifstream fixture(std::string(KPENGINE_TERRAIN_FIXTURE_DIR) +
            "/island_macro_256.terrainrecipe.json");
        ASSERT_TRUE(fixture.good());
        TerrainRecipe recipe = TerrainRecipe::FromJson(nlohmann::json::parse(fixture));
        const GridDomain2D domain = recipe.domain;
        recipe.nodes.push_back({"thermal", "terrain.erosion.thermal_flux", 1,
            {{"talus_angle_degrees", 28.0}, {"thermal_rate", 0.2}, {"iterations", 20}},
            {{"source", {"state", "surface_height_m"}}}});
        const auto scalar = [&recipe](const char *id, const float value) {
            recipe.nodes.push_back({id, "terrain.scalar.constant", 1,
                {{"value", value}}, {}});
        };
        scalar("soil", 0.3f);
        scalar("sand", 0.0f);
        scalar("water", 0.0f);
        scalar("suspended", 0.0f);
        scalar("rain", 0.02f);
        scalar("erodibility", 0.8f);
        scalar("hardness", 0.25f);
        scalar("obstacle", 0.0f);
        recipe.nodes.push_back({"state", "terrain.state.layered_heightfield", 1,
            nlohmann::json::object(),
            {{"bedrock_elevation_m", {"macro_landforms", "height"}},
             {"soil_thickness_m", {"soil", "value"}},
             {"sand_thickness_m", {"sand", "value"}},
             {"water_depth_m", {"water", "value"}},
             {"suspended_sediment_kg_per_m2", {"suspended", "value"}}}});
        recipe.nodes.push_back({"hydraulic", "terrain.erosion.hydraulic_pipe", 1,
            {{"duration_s", 1.0}, {"maximum_timestep_s", 0.05},
             {"capacity_kg_s_per_m3", 8.0}, {"dissolution_rate_per_s", 0.8},
             {"deposition_rate_per_s", 1.0}, {"boundary", "open"}},
            {{"state", {"state", "state"}},
             {"rain_rate_m_per_s", {"rain", "value"}},
             {"erodibility_0_1", {"erodibility", "value"}},
             {"hardness_0_1", {"hardness", "value"}},
             {"obstacle_0_1", {"obstacle", "value"}}}});

        TerrainEvaluator evaluator(registry);
        const auto started = std::chrono::steady_clock::now();
        const auto result = evaluator.Evaluate(recipe);
        const double total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        const auto &before = *result.nodes.at("state").outputs.at("surface_height_m");
        const auto &thermal = *result.nodes.at("thermal").outputs.at("height");
        const auto &hydraulic = result.nodes.at("hydraulic");
        const auto &hydraulic_height = *hydraulic.outputs.at("surface_height_m");
        const auto &discharge = *hydraulic.outputs.at("discharge_m3_per_s");
        const auto &exported_water = *hydraulic.outputs.at("water_exported_m3");
        const auto &water_budget = *hydraulic.outputs.at("water_budget_relative_residual");
        const auto &solid_budget = *hydraulic.outputs.at("solid_budget_relative_residual");

        const auto rmse = [](const TerrainValue2D &a, const TerrainValue2D &b) {
            double squared = 0.0;
            for (std::size_t i = 0; i < a.Samples().size(); ++i)
            {
                const double delta = static_cast<double>(a.Samples()[i]) - b.Samples()[i];
                squared += delta * delta;
            }
            return std::sqrt(squared / static_cast<double>(a.Samples().size()));
        };
        const double thermal_delta = rmse(before, thermal);
        const double hydraulic_delta = rmse(before, hydraulic_height);
        const float peak_discharge = *std::max_element(discharge.Samples().begin(), discharge.Samples().end());
        const float total_exported_water = std::accumulate(exported_water.Samples().begin(),
            exported_water.Samples().end(), 0.0f);
        EXPECT_GT(thermal_delta, 1.0e-4);
        EXPECT_GT(hydraulic_delta, 1.0e-4);
        EXPECT_GT(peak_discharge, 0.0f);
        EXPECT_LE(std::abs(water_budget.Samples().front()), 2.0e-6f);
        EXPECT_LE(std::abs(solid_budget.Samples().front()), 2.0e-6f);
        EXPECT_TRUE(hydraulic.outputs.at("state")->AsLayeredHeightfield());
        EXPECT_EQ(hydraulic.outputs.at("height").get(),
            hydraulic.outputs.at("surface_height_m").get());
        EXPECT_NE(hydraulic.content_hash, 0u);
        char *output_path_buffer = nullptr;
        std::size_t output_path_length = 0;
        (void)_dupenv_s(&output_path_buffer, &output_path_length, "KP_TERRAIN_COMPARE_CSV");
        const std::string output_path = output_path_buffer != nullptr
            ? output_path_buffer : "";
        std::free(output_path_buffer);
        if (!output_path.empty())
        {
            std::ofstream output(output_path);
            ASSERT_TRUE(output) << "could not write terrain comparison CSV: " << output_path;
            output << "x,z,before_m,thermal_m,hydraulic_m\n";
            for (std::uint32_t z = 0; z < domain.height; ++z)
                for (std::uint32_t x = 0; x < domain.width; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(z) * domain.width + x;
                    output << x << ',' << z << ',' << before.Samples()[index] << ','
                        << thermal.Samples()[index] << ',' << hydraulic_height.Samples()[index] << '\n';
                }
        }
        std::cout << "Terrain hydrology comparison (Debug): " << domain.width << 'x'
            << domain.height << " total=" << total_ms
            << " ms, thermal-node=" << result.nodes.at("thermal").evaluation_time_ms
            << " ms, hydraulic-node=" << hydraulic.evaluation_time_ms
            << " ms, thermal-RMSE=" << thermal_delta
            << " m, hydraulic-RMSE=" << hydraulic_delta
            << " m, peak-discharge=" << peak_discharge
            << " m^3/s, exported-water=" << total_exported_water
            << " m^3, water-budget-relative=" << water_budget.Samples().front()
            << ", solid-budget-relative=" << solid_budget.Samples().front() << '\n';
    }

    TEST(TerrainEvaluationTest, ExecutionControlStepsOneNodeAtATime)
    {
        EvaluationExecutionControl control;
        control.Pause();
        std::atomic_bool cancelled{false};
        std::atomic_bool waiting{false};
        std::atomic_int completed_nodes{0};
        std::thread worker([&]
        {
            waiting.store(true, std::memory_order_release);
            if (!control.WaitForNode(&cancelled)) return;
            completed_nodes.store(1, std::memory_order_release);
            if (!control.WaitForNode(&cancelled)) return;
            completed_nodes.store(2, std::memory_order_release);
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!waiting.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        if (!waiting.load(std::memory_order_acquire))
        {
            control.Resume();
            worker.join();
            FAIL() << "worker did not reach the paused node boundary";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(completed_nodes.load(std::memory_order_acquire), 0);

        control.Step();
        while (completed_nodes.load(std::memory_order_acquire) < 1 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        EXPECT_EQ(completed_nodes.load(std::memory_order_acquire), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(completed_nodes.load(std::memory_order_acquire), 1);

        control.Step();
        while (completed_nodes.load(std::memory_order_acquire) < 2 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        control.Resume();
        worker.join();
        EXPECT_EQ(completed_nodes.load(std::memory_order_acquire), 2);
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

    TEST(TerrainExecutorTest, NewRevisionReplacesPendingJobBeforeCapacityCheck)
    {
        struct Gate
        {
            std::mutex mutex;
            std::condition_variable wake;
            bool entered = false;
            bool release = false;
        };
        const auto gate = std::make_shared<Gate>();
        auto registry = MakeRegistry();
        std::string diagnostic;
        ASSERT_TRUE(registry->Register({"test.blocking_scalar", 1, {},
            {{"value", PortType::ScalarField2D}},
            [gate](const OperatorContext &context, const nlohmann::json &parameters,
                   const OperatorInputs &) {
                {
                    std::unique_lock lock(gate->mutex);
                    gate->entered = true;
                    gate->wake.notify_all();
                    gate->wake.wait_for(lock, std::chrono::seconds(2),
                        [&] { return gate->release; });
                }
                const std::size_t count = context.domain.SampleCount(context.maximum_samples);
                std::string error;
                auto field = ScalarField2D::Create(context.domain,
                    std::vector<float>(count, parameters.at("value").get<float>()),
                    context.maximum_samples, error);
                if (!field) throw std::runtime_error(error);
                return OperatorOutputs{{"value", std::move(field)}};
            }}, diagnostic)) << diagnostic;

        GenerationExecutor executor(registry, 1, 1, 2);
        TerrainRecipe blocking = ConstantRecipe(1.0f);
        blocking.nodes.front().operator_id = "test.blocking_scalar";
        ASSERT_TRUE(executor.Submit(1, std::move(blocking)));
        {
            std::unique_lock lock(gate->mutex);
            ASSERT_TRUE(gate->wake.wait_for(lock, std::chrono::seconds(2),
                [&] { return gate->entered; }));
        }
        EXPECT_TRUE(executor.Submit(2, ConstantRecipe(2.0f)));
        const bool accepted_latest = executor.Submit(3, ConstantRecipe(3.0f));
        {
            std::lock_guard lock(gate->mutex);
            gate->release = true;
        }
        gate->wake.notify_all();
        EXPECT_TRUE(accepted_latest);

        GenerationJobResult result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while ((!executor.TryPop(result) || result.revision != 3) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_EQ(result.revision, 3u);
        EXPECT_TRUE(result.evaluation.succeeded) << result.evaluation.diagnostic;
        if (result.evaluation.succeeded)
            EXPECT_FLOAT_EQ(result.evaluation.nodes.at("source").outputs.at("value")->At(0, 0),
                            3.0f);
        executor.Shutdown();
    }
}
