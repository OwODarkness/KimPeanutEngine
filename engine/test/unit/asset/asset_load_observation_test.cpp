#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <exception>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_load_observation_internal.h"
#include "asset/asset_manager.h"
#include "asset/model.h"
#include "config/path.h"

namespace
{
    using namespace std::chrono_literals;
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetLoadDisposition;
    using kpengine::asset::AssetLoadPhase;
    using kpengine::asset::AssetLoadState;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetType;
    using kpengine::asset::detail::AssetLoadObservationClock;
    using kpengine::asset::detail::AssetLoadSessionState;

    struct TestClock
    {
        std::chrono::steady_clock::time_point now{};
    };

    std::shared_ptr<TestClock> MakeClock()
    {
        return std::make_shared<TestClock>();
    }

    AssetLoadObservationClock ClockFor(const std::shared_ptr<TestClock> &clock)
    {
        return [clock] { return clock->now; };
    }

    class AssetObservationFixture
    {
    public:
        AssetObservationFixture()
        {
            root_ = std::filesystem::path(kpengine::GetAssetDirectory()) /
                    (".test_asset_observation_" + std::to_string(counter_++));
            std::filesystem::create_directories(root_);
        }

        ~AssetObservationFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        std::filesystem::path Path(const char *name) const { return root_ / name; }

    private:
        inline static uint32_t counter_ = 0;
        std::filesystem::path root_;
    };

    void WriteText(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open()) << path.string();
        file << text;
    }

    bool HasSuffix(const std::string &value, const std::string &suffix)
    {
        return value.size() >= suffix.size() &&
               value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    const kpengine::asset::AssetLoadObservation *FindObservation(
        const kpengine::asset::AssetLoadSnapshot &snapshot,
        const std::string &path)
    {
        for (const auto &observation : snapshot.recent_terminal_operations)
        {
            if (HasSuffix(observation.display_path, path))
            {
                return &observation;
            }
        }
        return nullptr;
    }
}

TEST(AssetLoadObservationStateTest, PublishesLifecycleAndCopiesAreIndependent)
{
    const auto clock = MakeClock();
    AssetLoadSessionState state(17, ClockFor(clock));

    const auto root = state.BeginOperation("root.material", AssetType::KPAT_Material,
                                          std::nullopt);
    const auto child = state.BeginOperation("shader.shader", AssetType::KPAT_ShaderProgram,
                                           root);
    ASSERT_NE(root, 0u);
    ASSERT_NE(child, 0u);

    state.SetKnownChildren(root, 1);
    state.UpdatePhase(child, AssetLoadPhase::LoadSource);
    clock->now += 12us;

    auto active = state.GetSnapshot();
    ASSERT_EQ(active.summary.operations_started, 2u);
    ASSERT_EQ(active.summary.operations_active, 2u);
    ASSERT_EQ(active.active_operations.size(), 2u);
    EXPECT_GT(active.active_operations.front().timing.inclusive_elapsed_us, 0u);

    state.Complete(child, AssetLoadState::Succeeded,
                   AssetLoadDisposition::LoadedAndRegistered,
                   AssetID(3, 1, AssetType::KPAT_ShaderProgram), {}, {}, {});
    state.ChildCompleted(root);
    clock->now += 8us;
    state.Complete(root, AssetLoadState::Succeeded,
                   AssetLoadDisposition::LoadedAndRegistered,
                   AssetID(4, 1, AssetType::KPAT_Material), {}, {}, {});
    state.Seal();

    auto terminal = state.GetSnapshot();
    ASSERT_TRUE(terminal.terminal);
    ASSERT_TRUE(terminal.sealed);
    EXPECT_EQ(terminal.summary.operations_succeeded, 2u);
    EXPECT_EQ(terminal.summary.operations_active, 0u);
    ASSERT_EQ(terminal.recent_terminal_operations.size(), 2u);

    terminal.recent_terminal_operations.clear();
    terminal.summary.operations_succeeded = 0;
    const auto isolated = state.GetSnapshot();
    EXPECT_EQ(isolated.summary.operations_succeeded, 2u);
    EXPECT_EQ(isolated.recent_terminal_operations.size(), 2u);
}

TEST(AssetLoadObservationStateTest, ClockMayReenterSessionWithoutDeadlocking)
{
    std::weak_ptr<AssetLoadSessionState> weak_state;
    const AssetLoadObservationClock clock = [&weak_state]
    {
        if (const auto state = weak_state.lock())
        {
            (void)state->IsSealed();
        }
        return std::chrono::steady_clock::now();
    };
    const auto state = std::make_shared<AssetLoadSessionState>(19, clock);
    weak_state = state;

    const auto operation = state->BeginOperation(
        "reentrant.asset", AssetType::KPAT_Texture, std::nullopt);
    ASSERT_NE(operation, 0u);
    state->Complete(operation, AssetLoadState::Succeeded,
                    AssetLoadDisposition::LoadedAndRegistered,
                    AssetID(5, 1, AssetType::KPAT_Texture), {}, {}, {});
    state->Seal();
    EXPECT_TRUE(state->GetSnapshot().terminal);
}

TEST(AssetLoadObservationStateTest, BoundsActiveAndTerminalDetailsButKeepsAggregates)
{
    const auto clock = MakeClock();
    AssetLoadSessionState state(18, ClockFor(clock));

    std::vector<kpengine::asset::AssetLoadOperationID> operations;
    for (uint32_t index = 0; index < 20; ++index)
    {
        operations.push_back(state.BeginOperation(
            "active.asset", AssetType::KPAT_Texture, std::nullopt));
    }
    const auto active = state.GetSnapshot();
    EXPECT_EQ(active.active_operations.size(), 16u);
    EXPECT_EQ(active.omitted_active_operations, 4u);
    ASSERT_FALSE(active.active_operations.empty());
    EXPECT_EQ(active.active_operations.front().operation, operations.back());

    for (const auto operation : operations)
    {
        state.Complete(operation, AssetLoadState::Failed, std::nullopt, std::nullopt,
                       {}, {}, "first failure");
    }
    state.Seal();
    const auto terminal = state.GetSnapshot();
    EXPECT_TRUE(terminal.terminal);
    EXPECT_EQ(terminal.summary.operations_failed, 20u);
    EXPECT_EQ(terminal.recent_terminal_operations.size(), 16u);
    EXPECT_EQ(terminal.summary.first_failure, "first failure");
}

TEST(AssetLoadObservationTest, ObservesRecursiveMaterialDependency)
{
    AssetObservationFixture fixture;
    const std::filesystem::path shader = fixture.Path("dependency.shader");
    const std::filesystem::path material = fixture.Path("root.material");
    WriteText(shader, R"({"version": 1, "shaders": []})");
    WriteText(material, R"({
        "version": 1,
        "shader": "dependency.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {}
    })");

    AssetManager &manager = AssetManager::GetInstance();
    auto session = manager.BeginLoadObservation();
    const AssetID material_id = manager.LoadSync(material.string(), session);
    ASSERT_TRUE(material_id.IsValid());
    session.Seal();
    const auto snapshot = session.GetSnapshot();

    ASSERT_TRUE(snapshot.terminal);
    ASSERT_EQ(snapshot.summary.operations_started, 2u);
    EXPECT_EQ(snapshot.summary.operations_succeeded, 2u);
    const auto *root = FindObservation(snapshot, "root.material");
    const auto *child = FindObservation(snapshot, "dependency.shader");
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(root->known_children, 1u);
    EXPECT_EQ(root->completed_children, 1u);
    ASSERT_TRUE(child->parent.has_value());
    EXPECT_EQ(*child->parent, root->operation);
    EXPECT_EQ(root->disposition, AssetLoadDisposition::LoadedAndRegistered);
    EXPECT_TRUE(root->size_cost.source_file_bytes.has_value());

    AssetID shader_program_id = child->result.value_or(AssetID());
    manager.UnRegisterAsset(material_id);
    manager.UnRegisterAsset(shader_program_id);
}

TEST(AssetLoadObservationTest, CacheHitDoesNotProbeSourceOrDecodePayload)
{
    AssetObservationFixture fixture;
    const std::filesystem::path shader = fixture.Path("cached_dependency.shader");
    const std::filesystem::path material = fixture.Path("cached_root.material");
    WriteText(shader, R"({"version": 1, "shaders": []})");
    WriteText(material, R"({
        "version": 1,
        "shader": "cached_dependency.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {}
    })");

    AssetManager &manager = AssetManager::GetInstance();
    const AssetID material_id = manager.LoadSync(material.string());
    ASSERT_TRUE(material_id.IsValid());
    std::error_code error;
    std::filesystem::remove(material, error);

    auto session = manager.BeginLoadObservation();
    EXPECT_EQ(manager.LoadSync(material.string(), session), material_id);
    session.Seal();
    const auto snapshot = session.GetSnapshot();
    ASSERT_EQ(snapshot.recent_terminal_operations.size(), 1u);
    const auto &observation = snapshot.recent_terminal_operations.front();
    EXPECT_EQ(observation.disposition, AssetLoadDisposition::CacheHit);
    EXPECT_FALSE(observation.size_cost.source_file_bytes.has_value());
    EXPECT_FALSE(observation.size_cost.decoded_payload_bytes.has_value());
    EXPECT_EQ(observation.timing.source_load_us, 0u);
    EXPECT_EQ(observation.timing.registration_us, 0u);

    manager.UnRegisterAsset(material_id);
}

TEST(AssetLoadObservationTest, FailureDiagnosticsKeepRelativeDisplayPath)
{
    AssetManager &manager = AssetManager::GetInstance();
    const std::filesystem::path missing =
        std::filesystem::path(kpengine::GetAssetDirectory()) /
        ".test_asset_observation_paths" / "nested" / "missing.unknown";

    auto session = manager.BeginLoadObservation();
    EXPECT_FALSE(manager.LoadSync(missing.string(), session).IsValid());
    session.Seal();
    const auto snapshot = session.GetSnapshot();

    const auto *missing_observation = FindObservation(snapshot, "missing.unknown");
    ASSERT_NE(missing_observation, nullptr);
    EXPECT_NE(missing_observation->display_path.find(
                  ".test_asset_observation_paths/nested/missing.unknown"),
              std::string::npos);
    EXPECT_NE(missing_observation->diagnostic.find(
                  ".test_asset_observation_paths/nested/missing.unknown"),
              std::string::npos);

    auto directory_session = manager.BeginLoadObservation();
    EXPECT_FALSE(manager.LoadSync(kpengine::GetAssetDirectory(), directory_session).IsValid());
    directory_session.Seal();
    const auto directory_snapshot = directory_session.GetSnapshot();
    ASSERT_EQ(directory_snapshot.recent_terminal_operations.size(), 1u);
    EXPECT_EQ(directory_snapshot.recent_terminal_operations.front().display_path,
              "<asset directory>");
}

TEST(AssetLoadObservationTest, AsyncRootRemainsObservedAfterImmediateSeal)
{
    AssetObservationFixture fixture;
    const std::filesystem::path shader = fixture.Path("async_dependency.shader");
    const std::filesystem::path material = fixture.Path("async_root.material");
    WriteText(shader, R"({"version": 1, "shaders": []})");
    WriteText(material, R"({
        "version": 1,
        "shader": "async_dependency.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {}
    })");

    AssetManager &manager = AssetManager::GetInstance();
    auto session = manager.BeginLoadObservation();
    auto future = manager.LoadAsync(material.string(), session);
    session.Seal();
    const AssetID material_id = future.get();
    ASSERT_TRUE(material_id.IsValid());
    const auto snapshot = session.GetSnapshot();
    ASSERT_TRUE(snapshot.terminal);
    EXPECT_EQ(snapshot.summary.operations_started, 2u);
    EXPECT_EQ(snapshot.summary.operations_active, 0u);

    const auto *child = FindObservation(snapshot, "async_dependency.shader");
    ASSERT_NE(child, nullptr);
    manager.UnRegisterAsset(material_id);
    manager.UnRegisterAsset(child->result.value_or(AssetID()));
}

TEST(AssetLoadObservationTest, LoaderExceptionPublishesFailureAndRethrows)
{
    AssetObservationFixture fixture;
    const std::filesystem::path shader = fixture.Path("broken_dependency.shader");
    const std::filesystem::path material = fixture.Path("broken_root.material");
    WriteText(shader, "{ this is not json");
    WriteText(material, R"({
        "version": 1,
        "shader": "broken_dependency.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {}
    })");

    AssetManager &manager = AssetManager::GetInstance();
    auto session = manager.BeginLoadObservation();
    EXPECT_THROW(manager.LoadSync(material.string(), session), std::exception);
    session.Seal();
    const auto snapshot = session.GetSnapshot();
    ASSERT_TRUE(snapshot.terminal);
    EXPECT_EQ(snapshot.summary.operations_succeeded, 0u);
    EXPECT_EQ(snapshot.summary.operations_failed, 2u);
    EXPECT_FALSE(snapshot.summary.first_failure.empty());
    EXPECT_NE(FindObservation(snapshot, "broken_root.material"), nullptr);
    EXPECT_NE(FindObservation(snapshot, "broken_dependency.shader"), nullptr);
}

TEST(AssetLoadObservationTest, ConcurrentObservedRequestsRemainDistinctAndDeduplicated)
{
    AssetObservationFixture fixture;
    const std::filesystem::path shader = fixture.Path("concurrent_dependency.shader");
    const std::filesystem::path material = fixture.Path("concurrent_root.material");
    WriteText(shader, R"({"version": 1, "shaders": []})");
    WriteText(material, R"({
        "version": 1,
        "shader": "concurrent_dependency.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {}
    })");

    AssetManager &manager = AssetManager::GetInstance();
    auto session = manager.BeginLoadObservation();
    auto first = manager.LoadAsync(material.string(), session);
    auto second = manager.LoadAsync(material.string(), session);
    session.Seal();
    const AssetID first_id = first.get();
    const AssetID second_id = second.get();
    ASSERT_TRUE(first_id.IsValid());
    EXPECT_EQ(first_id, second_id);

    const auto snapshot = session.GetSnapshot();
    ASSERT_TRUE(snapshot.terminal);
    EXPECT_EQ(snapshot.summary.operations_active, 0u);
    EXPECT_GE(snapshot.summary.operations_started, 2u);
    uint32_t root_operations = 0;
    std::set<uint64_t> observed_results;
    for (const auto &observation : snapshot.recent_terminal_operations)
    {
        if (HasSuffix(observation.display_path, "concurrent_root.material"))
        {
            ++root_operations;
            if (observation.result)
            {
                observed_results.insert(observation.result->Pack());
            }
        }
    }
    EXPECT_EQ(root_operations, 2u);
    EXPECT_EQ(observed_results.size(), 1u);

    manager.UnRegisterAsset(first_id);
    for (const auto &observation : snapshot.recent_terminal_operations)
    {
        if (HasSuffix(observation.display_path, "concurrent_dependency.shader") &&
            observation.result)
        {
            manager.UnRegisterAsset(*observation.result);
        }
    }
}

TEST(AssetLoadObservationTest, ReportsModelDecodedPayloadBytes)
{
    AssetObservationFixture fixture;
    const std::filesystem::path model = fixture.Path("measured_model.obj");
    WriteText(model, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    AssetManager &manager = AssetManager::GetInstance();
    auto session = manager.BeginLoadObservation();
    const AssetID model_id = manager.LoadSync(model.string(), session);
    ASSERT_TRUE(model_id.IsValid());
    session.Seal();
    const auto snapshot = session.GetSnapshot();
    const auto *observation = FindObservation(snapshot, "measured_model.obj");
    ASSERT_NE(observation, nullptr);
    ASSERT_TRUE(observation->size_cost.source_file_bytes.has_value());
    ASSERT_TRUE(observation->size_cost.decoded_payload_bytes.has_value());
    EXPECT_GT(*observation->size_cost.decoded_payload_bytes, 0u);

    const auto model_resource = manager.GetResource<kpengine::asset::ModelResource>(model_id);
    ASSERT_NE(model_resource, nullptr);
    const AssetID mesh_id = model_resource->GetData(kpengine::asset::ModelGeometryType::KPMG_Mesh);
    manager.UnRegisterAsset(model_id);
    manager.UnRegisterAsset(mesh_id);
}
