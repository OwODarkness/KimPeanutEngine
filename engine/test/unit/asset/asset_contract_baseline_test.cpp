#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "asset/asset.h"
#include "asset/asset_manager.h"
#include "asset/common.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/texture.h"
#include "asset/utility.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetPayload;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::AssetTypeDescriptor;
    using kpengine::asset::AssetTypeRegistry;
    using kpengine::asset::MeshResource;
    using kpengine::asset::ModelResource;
    using kpengine::asset::TextureResource;

    struct TestPayload final : kpengine::asset::IAssetPayload
    {
        AssetType GetAssetType() const noexcept override
        {
            return AssetType::Undefined;
        }
    };

    struct RegistryPayload final : kpengine::asset::IAssetPayload
    {
        explicit RegistryPayload(AssetType type) : type(type) {}

        AssetType GetAssetType() const noexcept override
        {
            return type;
        }

        AssetType type;
    };

    constexpr AssetType kCustomTestAssetType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 7u);

    AssetRegisterInfo MakeTextureInfo(const char *path)
    {
        AssetRegisterInfo info{};
        info.resource = std::make_shared<TextureResource>();
        info.path = path;
        info.name = "AX1BaselineTexture";
        info.type = AssetType::KPAT_Texture;
        return info;
    }

    AssetRegisterInfo MakeMeshInfo(const char *path)
    {
        AssetRegisterInfo info{};
        info.resource = std::make_shared<MeshResource>();
        info.path = path;
        info.name = "AX1BaselineMesh";
        info.type = AssetType::KPAT_Mesh;
        return info;
    }

    AssetRegisterInfo MakeModelInfo(const char *path, AssetID dependency = AssetID{})
    {
        AssetRegisterInfo info{};
        info.resource = std::make_shared<ModelResource>();
        info.path = path;
        info.name = "AX1BaselineModel";
        info.type = AssetType::KPAT_Model;
        if (dependency.IsValid())
        {
            info.dependencies.push_back(dependency);
        }
        return info;
    }
}

TEST(AssetContractBaselineTest, TypeRegistryReservesCustomRangeAndSeals)
{
    AssetTypeRegistry registry;
    std::string diagnostic;
    AssetTypeDescriptor descriptor{};
    descriptor.type = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 3u);
    descriptor.name = "AX1_RegistryTest";
    descriptor.extensions = {"AX1REG"};
    descriptor.loader = [](const std::string &, AssetRegisterInfo &)
    { return false; };

    EXPECT_TRUE(kpengine::asset::IsCustomAssetType(descriptor.type));
    EXPECT_FALSE(kpengine::asset::IsAssetTypeValueInExtensionRange(
        static_cast<AssetType>(kpengine::asset::kLastBuiltInAssetTypeValue)));
    EXPECT_TRUE(registry.Register(descriptor, diagnostic)) << diagnostic;

    AssetTypeDescriptor duplicate_extension = descriptor;
    duplicate_extension.type = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 4u);
    duplicate_extension.name = "AX1_RegistryDuplicate";
    EXPECT_FALSE(registry.Register(std::move(duplicate_extension), diagnostic));

    EXPECT_TRUE(registry.Seal(diagnostic)) << diagnostic;
    AssetTypeDescriptor late_descriptor{};
    late_descriptor.type = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 5u);
    late_descriptor.name = "AX1_RegistryLate";
    late_descriptor.extensions = {"ax1late"};
    late_descriptor.loader = [](const std::string &, AssetRegisterInfo &)
    { return false; };
    EXPECT_FALSE(registry.Register(std::move(late_descriptor), diagnostic));
}

TEST(AssetContractBaselineTest, RegisteredCustomTypeLoadsThroughAssetCache)
{
    AssetManager &manager = AssetManager::GetInstance();
    std::string diagnostic;
    AssetTypeDescriptor descriptor{};
    descriptor.type = kCustomTestAssetType;
    descriptor.name = "AX1_CustomTest";
    descriptor.extensions = {"ax1custom"};
    descriptor.loader = [](const std::string &path, AssetRegisterInfo &info)
    {
        info.path = path;
        info.name = "AX1CustomPayload";
        info.type = kCustomTestAssetType;
        info.resource = std::make_shared<RegistryPayload>(kCustomTestAssetType);
        return true;
    };

    ASSERT_TRUE(manager.RegisterAssetType(std::move(descriptor), diagnostic)) << diagnostic;
    const AssetID id = manager.LoadSync("ax1_registry.ax1custom");
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(id.type, kCustomTestAssetType);
    const std::shared_ptr<RegistryPayload> payload =
        manager.GetResource<RegistryPayload>(id);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->GetAssetType(), kCustomTestAssetType);

    manager.UnRegisterAsset(id);
    EXPECT_EQ(manager.GetAsset(id), nullptr);
}

TEST(AssetContractBaselineTest, BuiltInAssetTypeValuesRemainStable)
{
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::Undefined), 0u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Model), 1u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Texture), 2u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Audio), 3u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Shader), 4u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_ShaderProgram), 5u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Mesh), 6u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Material), 7u);
    EXPECT_EQ(static_cast<std::uint16_t>(AssetType::KPAT_Level), 8u);
}

TEST(AssetContractBaselineTest, AssetIdPackHasStableGoldenValue)
{
    const AssetID original{0x12345678u, 0xbeefu, AssetType::KPAT_Material};

    EXPECT_EQ(original.Pack(), 0x0007beef12345678ULL);
    EXPECT_EQ(AssetID::Unpack(original.Pack()), original);
}

TEST(AssetContractBaselineTest, BuiltInExtensionRoutingRemainsStable)
{
    EXPECT_EQ(kpengine::asset::GetFileExtension("Scene.MODEL"), "model");
    EXPECT_EQ(kpengine::asset::ExtractAssetType("model"), AssetType::KPAT_Model);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("texture"), AssetType::KPAT_Texture);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("png"), AssetType::KPAT_Texture);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("wav"), AssetType::KPAT_Audio);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("vert"), AssetType::KPAT_Shader);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("shader"),
              AssetType::KPAT_ShaderProgram);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("material"), AssetType::KPAT_Material);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("level"), AssetType::KPAT_Level);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("model3.json"), AssetType::Undefined);
    EXPECT_EQ(kpengine::asset::ExtractAssetType("unknown"), AssetType::Undefined);
}

TEST(AssetContractBaselineTest, TypedPayloadAccessPreservesSharedLifetime)
{
    AssetManager &manager = AssetManager::GetInstance();
    AssetRegisterInfo info = MakeTextureInfo("ax1_baseline_payload.texture");
    const AssetID id = manager.RegisterAsset(info);
    ASSERT_TRUE(id.IsValid());

    const std::shared_ptr<TextureResource> texture =
        manager.GetResource<TextureResource>(id);
    ASSERT_NE(texture, nullptr);
    EXPECT_NE(texture->data, nullptr);
    EXPECT_EQ(manager.GetResource<MeshResource>(id), nullptr);

    manager.UnRegisterAsset(id);
    EXPECT_EQ(manager.GetAsset(id), nullptr);
    EXPECT_NE(texture, nullptr);
    EXPECT_NE(texture->data, nullptr);
}

TEST(AssetContractBaselineTest, ExtensionlessBuiltInMeshCanRegisterAsSubresource)
{
    AssetManager &manager = AssetManager::GetInstance();
    AssetRegisterInfo info = MakeMeshInfo("ax1_baseline_mesh.mesh");
    const AssetID id = manager.RegisterAsset(info);
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(id.type, AssetType::KPAT_Mesh);
    manager.UnRegisterAsset(id);
}

TEST(AssetContractBaselineTest, PolymorphicPayloadAcceptsExternalConcreteTypes)
{
    const AssetPayload payload = std::make_shared<TestPayload>();

    ASSERT_TRUE(kpengine::asset::IsValidResource(payload));
    const std::shared_ptr<TestPayload> typed =
        std::dynamic_pointer_cast<TestPayload>(payload);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->GetAssetType(), AssetType::Undefined);
    EXPECT_FALSE(kpengine::asset::IsValidResource(AssetPayload{}));
}

TEST(AssetContractBaselineTest, DependenciesPreventPrematureUnload)
{
    AssetManager &manager = AssetManager::GetInstance();
    const std::size_t total_before = manager.GetTotalLiveAssetCount();
    AssetRegisterInfo texture_info = MakeTextureInfo("ax1_baseline_dependency.texture");
    const AssetID texture_id = manager.RegisterAsset(texture_info);
    ASSERT_TRUE(texture_id.IsValid());
    EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before + 1);

    AssetRegisterInfo model_info = MakeModelInfo("ax1_baseline_owner.model", texture_id);
    const AssetID model_id = manager.RegisterAsset(model_info);
    ASSERT_TRUE(model_id.IsValid());
    EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before + 2);

    manager.UnRegisterAsset(texture_id);
    EXPECT_NE(manager.GetAsset(texture_id), nullptr);

    manager.UnRegisterAsset(model_id);
    EXPECT_EQ(manager.GetAsset(model_id), nullptr);

    manager.UnRegisterAsset(texture_id);
    EXPECT_EQ(manager.GetAsset(texture_id), nullptr);
    EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before);
}

TEST(AssetContractBaselineTest, InvalidOwnedChildRollsBackWithoutCacheResidue)
{
    AssetManager &manager = AssetManager::GetInstance();
    const std::size_t mesh_count = manager.GetLiveAssetCount(AssetType::KPAT_Mesh);

    AssetRegisterInfo info = MakeModelInfo("ax1_baseline_invalid_child.model");
    info.owned_children.push_back({{}, "ax1_baseline_invalid_child.model#mesh",
                                   "AX1BaselineInvalidChild", {}, AssetType::KPAT_Mesh});

    EXPECT_FALSE(manager.RegisterAsset(info).IsValid());
    EXPECT_EQ(manager.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count);
}
