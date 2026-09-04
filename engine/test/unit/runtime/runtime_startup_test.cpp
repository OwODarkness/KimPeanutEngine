#include <memory>
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
