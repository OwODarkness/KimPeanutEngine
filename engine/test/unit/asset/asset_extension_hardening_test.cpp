#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "asset/asset.h"
#include "asset/asset_load_observation.h"
#include "asset/asset_manager.h"
#include "asset/asset_type_registry.h"
#include "asset/common.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetPayload;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::AssetTypeDescriptor;
    using kpengine::asset::IAssetPayload;

    constexpr AssetType kValidType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 0x20u);
    constexpr AssetType kMalformedType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 0x21u);
    constexpr AssetType kThrowingType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 0x22u);
    constexpr AssetType kDependencyType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 0x23u);
    constexpr AssetType kConcurrentType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 0x24u);

    struct RegistryPayload final : IAssetPayload
    {
        explicit RegistryPayload(AssetType asset_type) : type(asset_type) {}

        AssetType GetAssetType() const noexcept override
        {
            return type;
        }

        AssetType type;
    };

    struct ConcurrentLoaderState
    {
        std::atomic<int> active{0};
        std::atomic<int> maximum_active{0};
        std::atomic<int> calls{0};
    };

    void UpdateMaximum(std::atomic<int> &maximum, int value)
    {
        int observed = maximum.load(std::memory_order_relaxed);
        while (value > observed &&
               !maximum.compare_exchange_weak(observed, value,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed))
        {
        }
    }

    AssetTypeDescriptor MakeDescriptor(
        AssetType type,
        const char *name,
        const char *extension,
        kpengine::asset::AssetLoaderCallback loader)
    {
        AssetTypeDescriptor descriptor{};
        descriptor.type = type;
        descriptor.name = name;
        descriptor.extensions = {extension};
        descriptor.loader = std::move(loader);
        return descriptor;
    }

    class AssetExtensionHardeningTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            AssetManager &manager = AssetManager::GetInstance();
            std::string diagnostic;
            concurrent_state_ = std::make_shared<ConcurrentLoaderState>();

            ASSERT_TRUE(manager.RegisterAssetType(
                MakeDescriptor(
                    kValidType, "AX1_4_Valid", "ax14valid",
                    [](const std::string &path, AssetRegisterInfo &info)
                    {
                        info.path = path;
                        info.name = "AX1_4_ValidPayload";
                        info.type = kValidType;
                        info.resource = std::make_shared<RegistryPayload>(kValidType);
                        return true;
                    }),
                diagnostic))
                << diagnostic;

            ASSERT_TRUE(manager.RegisterAssetType(
                MakeDescriptor(
                    kMalformedType, "AX1_4_Malformed", "ax14malformed",
                    [](const std::string &path, AssetRegisterInfo &info)
                    {
                        info.path = path;
                        info.name = "AX1_4_WrongPayload";
                        info.type = kMalformedType;
                        info.resource = std::make_shared<RegistryPayload>(kValidType);
                        return true;
                    }),
                diagnostic))
                << diagnostic;

            ASSERT_TRUE(manager.RegisterAssetType(
                MakeDescriptor(
                    kThrowingType, "AX1_4_Throwing", "ax14throw",
                    [](const std::string &, AssetRegisterInfo &)
                    {
                        throw std::runtime_error("AX1.4 test loader failure");
                        return false;
                    }),
                diagnostic))
                << diagnostic;

            ASSERT_TRUE(manager.RegisterAssetType(
                MakeDescriptor(
                    kDependencyType, "AX1_4_Dependency", "ax14dependency",
                    [](const std::string &path, AssetRegisterInfo &info)
                    {
                        info.path = path;
                        info.name = "AX1_4_MissingDependencyParent";
                        info.type = kDependencyType;
                        info.resource = std::make_shared<RegistryPayload>(kDependencyType);
                        info.dependency_requests.push_back({
                            "ax14_missing_dependency.ax14missing",
                            kDependencyType});
                        return true;
                    }),
                diagnostic))
                << diagnostic;

            ASSERT_TRUE(manager.RegisterAssetType(
                MakeDescriptor(
                    kConcurrentType, "AX1_4_Concurrent", "ax14concurrent",
                    [state = concurrent_state_](const std::string &path,
                                                AssetRegisterInfo &info)
                    {
                        const int active =
                            state->active.fetch_add(1, std::memory_order_relaxed) + 1;
                        UpdateMaximum(state->maximum_active, active);
                        state->calls.fetch_add(1, std::memory_order_relaxed);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        state->active.fetch_sub(1, std::memory_order_relaxed);

                        info.path = path;
                        info.name = "AX1_4_ConcurrentPayload";
                        info.type = kConcurrentType;
                        info.resource = std::make_shared<RegistryPayload>(kConcurrentType);
                        return true;
                    }),
                diagnostic))
                << diagnostic;
        }

        static std::shared_ptr<ConcurrentLoaderState> concurrent_state_;
    };

    std::shared_ptr<ConcurrentLoaderState>
        AssetExtensionHardeningTest::concurrent_state_;
}

TEST_F(AssetExtensionHardeningTest, ValidExternalTypeLoadsAndUnloads)
{
    AssetManager &manager = AssetManager::GetInstance();
    const AssetID id = manager.LoadSync("ax14_valid.ax14valid");

    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(id.type, kValidType);
    ASSERT_NE(manager.GetResource<RegistryPayload>(id), nullptr);
    EXPECT_EQ(manager.GetLiveAssetCount(kValidType), 1u);

    manager.UnRegisterAsset(id);
    EXPECT_EQ(manager.GetAsset(id), nullptr);
    EXPECT_EQ(manager.GetResource<RegistryPayload>(id), nullptr);
    EXPECT_EQ(manager.GetLiveAssetCount(kValidType), 0u);

    manager.UnRegisterAsset(id);
}

TEST_F(AssetExtensionHardeningTest, MalformedPayloadIsRejectedWithoutCaching)
{
    AssetManager &manager = AssetManager::GetInstance();
    const AssetID id = manager.LoadSync("ax14_malformed.ax14malformed");

    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(manager.GetLiveAssetCount(kMalformedType), 0u);
}

TEST_F(AssetExtensionHardeningTest, ThrowingLoaderDoesNotPublishPartialAsset)
{
    AssetManager &manager = AssetManager::GetInstance();

    EXPECT_THROW(
        manager.LoadSync("ax14_throwing.ax14throw"),
        std::runtime_error);
    EXPECT_EQ(manager.GetLiveAssetCount(kThrowingType), 0u);
}

TEST_F(AssetExtensionHardeningTest, DependencyFailureDoesNotPublishParent)
{
    AssetManager &manager = AssetManager::GetInstance();
    const AssetID id = manager.LoadSync("ax14_parent.ax14dependency");

    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(manager.GetLiveAssetCount(kDependencyType), 0u);
}

TEST_F(AssetExtensionHardeningTest, ConcurrentLoadsShareOneIdentityAndSerializeLoaderAccess)
{
    AssetManager &manager = AssetManager::GetInstance();
    const std::string path = "ax14_shared.ax14concurrent";

    auto first = manager.LoadAsync(path);
    auto second = manager.LoadAsync(path);
    const AssetID first_id = first.get();
    const AssetID second_id = second.get();

    ASSERT_TRUE(first_id.IsValid());
    EXPECT_EQ(second_id, first_id);
    EXPECT_EQ(manager.GetLiveAssetCount(kConcurrentType), 1u);
    EXPECT_EQ(concurrent_state_->maximum_active.load(), 1);
    EXPECT_GE(concurrent_state_->calls.load(), 1);

    manager.UnRegisterAsset(first_id);
}

TEST_F(AssetExtensionHardeningTest, AsyncObservationUsesRegisteredCustomType)
{
    AssetManager &manager = AssetManager::GetInstance();
    auto session = manager.BeginLoadObservation();
    auto future = manager.LoadAsync("ax14_observed.ax14valid", session);
    session.Seal();
    const AssetID id = future.get();

    ASSERT_TRUE(id.IsValid());
    const auto snapshot = session.GetSnapshot();
    ASSERT_TRUE(snapshot.terminal);
    ASSERT_EQ(snapshot.recent_terminal_operations.size(), 1u);
    EXPECT_EQ(snapshot.recent_terminal_operations.front().expected_type, kValidType);
    EXPECT_EQ(snapshot.recent_terminal_operations.front().result, id);

    manager.UnRegisterAsset(id);
}

TEST_F(AssetExtensionHardeningTest, DirectRegistrationRejectsMismatchedPayload)
{
    AssetManager &manager = AssetManager::GetInstance();
    AssetRegisterInfo info{};
    info.path = "ax14_direct.ax14valid";
    info.name = "AX1_4_DirectMismatch";
    info.type = kMalformedType;
    info.resource = std::make_shared<RegistryPayload>(kValidType);

    EXPECT_FALSE(manager.RegisterAsset(info).IsValid());
    EXPECT_EQ(manager.GetLiveAssetCount(kMalformedType), 0u);
}
