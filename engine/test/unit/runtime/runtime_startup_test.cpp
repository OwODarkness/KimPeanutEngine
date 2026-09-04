#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "engine.h"
#include "level/level_instance.h"
#include "runtime_global_context.h"
#include "render/render_system.h"
#include "script/lua/lua_vm.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::LevelCameraRecord;
    using kpengine::asset::LevelObject;
    using kpengine::asset::LevelResource;

    class StartupAssetFixture final
    {
    public:
        ~StartupAssetFixture()
        {
            for (const AssetID id : levels_)
            {
                assets_.UnRegisterAsset(id);
            }
            if (material_.IsValid())
            {
                assets_.UnRegisterAsset(material_);
            }
        }

        AssetID AddLevel(std::vector<LevelObject> objects)
        {
            auto resource = std::make_shared<LevelResource>();
            resource->objects = std::move(objects);

            AssetRegisterInfo info{};
            info.resource = std::move(resource);
            info.path = "runtime_startup_test_" + std::to_string(levels_.size()) + ".level";
            info.name = info.path;
            info.type = AssetType::KPAT_Level;
            const AssetID id = assets_.RegisterAsset(info);
            levels_.push_back(id);
            return id;
        }

        AssetID AddWrongTypeAsset()
        {
            AssetRegisterInfo info{};
            info.resource = std::make_shared<kpengine::asset::MaterialResource>();
            info.path = "runtime_startup_test_wrong_type.material";
            info.name = info.path;
            info.type = AssetType::KPAT_Material;
            material_ = assets_.RegisterAsset(info);
            return material_;
        }

        static LevelCameraRecord Camera()
        {
            LevelCameraRecord camera{};
            camera.id = "startup-camera";
            camera.enabled = true;
            return camera;
        }

        AssetManager &assets_ = AssetManager::GetInstance();
        std::vector<AssetID> levels_;
        AssetID material_;
    };

    void DisableUninitializedScriptServices(kpengine::runtime::RuntimeContext &context)
    {
        // FinalizeGameStartup is intentionally headless here. Lua command
        // binding belongs to the initialized Engine path, not this seam test.
        context.lua_vm_.reset();
        context.command_registry_.reset();
    }
}

TEST(RuntimeStartupTest, CommitsAValidCameraLevel)
{
    StartupAssetFixture assets;
    kpengine::runtime::RuntimeContext context;
    DisableUninitializedScriptServices(context);
    context.SetStartupControllerSetupOverride(
        [](kpengine::gameplay::GameplayWorld &, kpengine::input::InputSystem *,
           kpengine::gameplay::ActorHandle)
        { return true; });
    const AssetID level = assets.AddLevel({StartupAssetFixture::Camera()});
    context.SetStartupLevel(level);

    const auto result = context.FinalizeGameStartup();

    ASSERT_TRUE(result) << result.diagnostic;
    ASSERT_NE(context.level_instance_, nullptr);
    EXPECT_TRUE(context.level_instance_->IsActive());
    EXPECT_EQ(context.level_instance_->GetActorCount(), 1U);
}

TEST(RuntimeStartupTest, RejectsCameraFreeLevelAndUnloadsTheAttempt)
{
    StartupAssetFixture assets;
    kpengine::runtime::RuntimeContext context;
    DisableUninitializedScriptServices(context);
    context.SetStartupControllerSetupOverride(
        [](kpengine::gameplay::GameplayWorld &, kpengine::input::InputSystem *,
           kpengine::gameplay::ActorHandle)
        { return true; });
    context.SetStartupLevel(assets.AddLevel({}));

    const auto result = context.FinalizeGameStartup();

    EXPECT_FALSE(result);
    EXPECT_NE(result.diagnostic.find("enabled camera"), std::string::npos);
    ASSERT_NE(context.level_instance_, nullptr);
    EXPECT_FALSE(context.level_instance_->IsActive());
    EXPECT_EQ(context.level_instance_->GetActorCount(), 0U);
}

TEST(RuntimeStartupTest, RejectsStaleAndWrongTypedStartupIDs)
{
    StartupAssetFixture assets;
    {
        kpengine::runtime::RuntimeContext context;
        DisableUninitializedScriptServices(context);
        context.SetStartupLevel(AssetID{});
        const auto result = context.FinalizeGameStartup();
        EXPECT_FALSE(result);
        EXPECT_NE(result.diagnostic.find("invalid"), std::string::npos);
    }
    {
        kpengine::runtime::RuntimeContext context;
        DisableUninitializedScriptServices(context);
        context.SetStartupLevel(assets.AddWrongTypeAsset());
        const auto result = context.FinalizeGameStartup();
        EXPECT_FALSE(result);
        EXPECT_NE(result.diagnostic.find("invalid"), std::string::npos);
    }
}

TEST(RuntimeStartupTest, RollsBackWhenControllerPossessionFails)
{
    StartupAssetFixture assets;
    kpengine::runtime::RuntimeContext context;
    DisableUninitializedScriptServices(context);
    context.SetStartupControllerSetupOverride(
        [](kpengine::gameplay::GameplayWorld &, kpengine::input::InputSystem *,
           kpengine::gameplay::ActorHandle)
        { return false; });
    context.SetStartupLevel(assets.AddLevel({StartupAssetFixture::Camera()}));

    const auto result = context.FinalizeGameStartup();

    EXPECT_FALSE(result);
    EXPECT_NE(result.diagnostic.find("could not possess"), std::string::npos);
    ASSERT_NE(context.level_instance_, nullptr);
    EXPECT_FALSE(context.level_instance_->IsActive());
    EXPECT_EQ(context.level_instance_->GetActorCount(), 0U);
}

TEST(RuntimeStartupTest, ClearMakesEngineLifecycleTerminal)
{
    kpengine::runtime::Engine engine;
    engine.Clear();

    EXPECT_THROW(engine.Initialize(), std::runtime_error);
}

TEST(RuntimeFramePolicyTest, SkipsRecoverableBeginFailuresAndEscalatesInvalidStates)
{
    using kpengine::render::RenderSystemLifecycleState;
    using kpengine::runtime::RenderFrameBeginDisposition;
    using kpengine::runtime::ClassifyRenderFrameBegin;

    EXPECT_EQ(ClassifyRenderFrameBegin(false, RenderSystemLifecycleState::Ready),
              RenderFrameBeginDisposition::SkipRecoverable);
    EXPECT_EQ(ClassifyRenderFrameBegin(false, RenderSystemLifecycleState::FrameActive),
              RenderFrameBeginDisposition::Fatal);
    EXPECT_EQ(ClassifyRenderFrameBegin(true, RenderSystemLifecycleState::FrameActive),
              RenderFrameBeginDisposition::Record);
}

TEST(RuntimeStartupCoordinatorTest, PublishesOrderedPhasesAndCopiedSnapshots)
{
    kpengine::runtime::StartupCoordinator coordinator;
    const uint64_t transaction = coordinator.Begin();

    EXPECT_EQ(coordinator.GetSnapshot().transaction_id, transaction);
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::PresentationStarting));
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::PresentationReady));
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::LoadingAssets));
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::PreparingCpuArtifacts));
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::PromotingSceneRenderer));
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::InstantiatingLevel));
    EXPECT_TRUE(coordinator.SetPhase(kpengine::runtime::StartupPhase::ActivatingEditorWorkspace));
    coordinator.SetReady();

    const auto snapshot = coordinator.GetSnapshot();
    EXPECT_EQ(snapshot.phase, kpengine::runtime::StartupPhase::Ready);
    EXPECT_FLOAT_EQ(snapshot.progress.fraction, 1.0f);
    EXPECT_FALSE(coordinator.SetPhase(kpengine::runtime::StartupPhase::LoadingAssets));
}

TEST(RuntimeStartupCoordinatorTest, WakesWaitersAndPreservesFirstFailure)
{
    kpengine::runtime::StartupCoordinator coordinator;
    coordinator.Begin();
    const auto initial = coordinator.GetSnapshot();
    kpengine::runtime::StartupSnapshot observed;
    std::thread waiter([&]
                        { observed = coordinator.WaitForRevision(initial.revision); });

    coordinator.SetProgress({2, 4, true, 0.5f});
    waiter.join();
    EXPECT_EQ(observed.progress.completed_units, 2U);
    EXPECT_FLOAT_EQ(observed.progress.fraction, 0.5f);

    coordinator.Fail("first failure");
    coordinator.Fail("later failure");
    EXPECT_EQ(coordinator.GetSnapshot().diagnostic, "first failure");
    coordinator.Rollback();
    EXPECT_EQ(coordinator.GetSnapshot().phase, kpengine::runtime::StartupPhase::RolledBack);
}

TEST(RuntimeStartupCoordinatorTest, TerminalStateIsImmutableToOrdinaryMutators)
{
    const auto session = AssetManager::GetInstance().BeginLoadObservation();
    kpengine::runtime::StartupCoordinator coordinator;
    coordinator.Begin();
    coordinator.SetAssetSession(session);
    coordinator.SetPhase(kpengine::runtime::StartupPhase::PresentationStarting);
    coordinator.Fail("startup failed");
    const auto failed = coordinator.GetSnapshot();

    coordinator.SetProgress({9, 10, true, 0.9f});
    coordinator.SetAssetSession({});
    coordinator.Fail("replacement failure");
    coordinator.Cancel("late cancellation");
    const auto unchanged = coordinator.GetSnapshot();

    EXPECT_EQ(unchanged.phase, kpengine::runtime::StartupPhase::Failed);
    EXPECT_EQ(unchanged.diagnostic, "startup failed");
    EXPECT_EQ(unchanged.revision, failed.revision);
    ASSERT_TRUE(unchanged.asset.has_value());
    EXPECT_TRUE(unchanged.asset->sealed);
    EXPECT_TRUE(unchanged.asset->terminal);
    coordinator.Rollback();
    const auto rolled_back = coordinator.GetSnapshot();
    coordinator.SetProgress({10, 10, true, 1.0f});
    EXPECT_EQ(coordinator.GetSnapshot().revision, rolled_back.revision);
}

TEST(RuntimeStartupCoordinatorTest, WaiterObservesNestedAssetRevisionChanges)
{
    auto &assets = AssetManager::GetInstance();
    auto resource = std::make_shared<kpengine::asset::MaterialResource>();
    AssetRegisterInfo info{};
    info.resource = resource;
    info.path = "runtime_startup_observation_waiter.material";
    info.name = info.path;
    info.type = AssetType::KPAT_Material;
    const AssetID material = assets.RegisterAsset(info);
    ASSERT_TRUE(material.IsValid());

    const auto session = assets.BeginLoadObservation();
    kpengine::runtime::StartupCoordinator coordinator;
    coordinator.Begin();
    coordinator.SetAssetSession(session);
    const auto initial = coordinator.GetSnapshot();
    ASSERT_TRUE(initial.asset.has_value());

    kpengine::runtime::StartupSnapshot observed;
    std::thread waiter([&]
                        {
                            observed = coordinator.WaitForRevision(
                                initial.revision, initial.asset->revision);
                        });
    assets.LoadSync(info.path, session);
    waiter.join();

    ASSERT_TRUE(observed.asset.has_value());
    EXPECT_GT(observed.asset->revision, initial.asset->revision);
    assets.UnRegisterAsset(material);
}

TEST(RuntimeStartupCoordinatorTest, StartupAccessBarrierWaitsForMainLaneToQuiesce)
{
    kpengine::runtime::StartupAccessBarrier barrier;
    barrier.Begin();
    std::atomic_bool waiting{false};
    std::atomic_bool released{false};
    std::thread waiter([&]
                        {
                            waiting.store(true, std::memory_order_release);
                            barrier.WaitForEnd();
                            released.store(true, std::memory_order_release);
                        });

    while (!waiting.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    EXPECT_FALSE(released.load(std::memory_order_acquire));
    barrier.End();
    waiter.join();
    EXPECT_TRUE(released.load(std::memory_order_acquire));
}
