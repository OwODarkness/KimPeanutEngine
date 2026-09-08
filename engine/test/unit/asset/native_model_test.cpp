#include <array>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "image_io/image_io.h"
#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/model_archive.h"
#include "asset/native_model.h"
#include "asset/native_model_loader.h"
#include "config/path.h"

#ifndef KPENGINE_NATIVE_RUNTIME_FIXTURE_DIR
#define KPENGINE_NATIVE_RUNTIME_FIXTURE_DIR ""
#endif

namespace
{
    using kpengine::asset::AssetType;
    using kpengine::asset::AssetID;
    using kpengine::asset::Asset;
    using kpengine::Vector2f;
    using kpengine::Vector3f;
    using kpengine::asset::ContentHash;
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::ProductRelativePath;
    using kpengine::asset::NativeModelData;
    using kpengine::asset::NativeModelError;
    using kpengine::asset::NativeModelErrorCode;
    using kpengine::asset::NativeModelMaterialReference;
    using kpengine::asset::SerializeNativeModel;
    using kpengine::asset::DeserializeNativeModel;

    NativeModelData MakeModel()
    {
        NativeModelData model;
        model.vertices = {
            {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        };
        model.indices = {0, 1, 2};
        model.sections = {{0, 3, 0, {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}}};
        model.local_bounds = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
        model.material_references = {{AssetType::KPAT_Material,
                                      kpengine::asset::Sha256("material-slot-0")}};
        return model;
    }

    NativeModelErrorCode CatchNativeModelError(const std::vector<std::byte> &bytes)
    {
        try
        {
            (void)DeserializeNativeModel(bytes);
        }
        catch (const NativeModelError &error)
        {
            return error.Code();
        }
        ADD_FAILURE() << "expected NativeModelError";
        return NativeModelErrorCode::InvalidArgument;
    }

    void WriteBytes(const std::filesystem::path &path, const std::vector<std::byte> &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << path.string();
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file.good()) << path.string();
    }

    void WriteText(const std::filesystem::path &path, const std::string &text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << path.string();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        ASSERT_TRUE(file.good()) << path.string();
    }

    std::filesystem::path ArchiveModelPath(const std::vector<std::byte> &bytes)
    {
        return std::filesystem::path(kpengine::GetAssetDirectory()) / ".archive" / "models" /
               (kpengine::asset::Sha256(bytes).ToHex() + ".model");
    }

    NativeModelData MakeModelWithoutMaterials()
    {
        NativeModelData model = MakeModel();
        model.material_references.clear();
        return model;
    }

    std::string ValidTestMaterial()
    {
        return R"({
            "version": 1,
            "shader": "../../shader/simple_triangle.shader",
            "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
            "parameters": {}
        })";
    }

    std::vector<std::byte> BytesFromText(const std::string &text)
    {
        return {reinterpret_cast<const std::byte *>(text.data()),
                reinterpret_cast<const std::byte *>(text.data() + text.size())};
    }

    std::string ReadText(const std::filesystem::path &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    NativeModelData MakeMultiMaterialModel(const ContentHash &first_material,
                                           const ContentHash &second_material)
    {
        NativeModelData model = MakeModelWithoutMaterials();
        model.indices = {0, 1, 2, 0, 2, 1};
        model.sections = {{0, 3, 0, {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}},
                          {3, 3, 1, {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}}};
        model.material_references = {{AssetType::KPAT_Material, first_material},
                                     {AssetType::KPAT_Material, second_material}};
        return model;
    }

    std::filesystem::path WriteNativeRuntimeTexture()
    {
        const std::filesystem::path path = std::filesystem::path(kpengine::GetAssetDirectory()) /
                                            ".test_native_runtime" / "multi_material.png";
        std::filesystem::create_directories(path.parent_path());
        kpengine::image_io::ImageBuffer image;
        image.width = 1;
        image.height = 1;
        image.pixels = {255, 128, 32, 255};
        const auto result = kpengine::image_io::WritePng(image, path.string());
        EXPECT_TRUE(result.success) << result.diagnostic;
        return path;
    }
}

TEST(NativeModelFormatTest, SerializesDeterministicallyAndRoundTrips)
{
    const NativeModelData source = MakeModel();
    const std::vector<std::byte> first = SerializeNativeModel(source);
    const std::vector<std::byte> second = SerializeNativeModel(source);

    ASSERT_EQ(first, second);
    ASSERT_GE(first.size(), kpengine::asset::kNativeModelHeaderSize);
    EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(first[0])), 'K');
    EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(first[1])), 'P');
    EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(first[2])), 'M');
    EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(first[3])), 'O');
    EXPECT_EQ(first[8], static_cast<std::byte>(kpengine::asset::kNativeModelVersion));
    EXPECT_EQ(first[9], std::byte{0});

    const auto product = DeserializeNativeModel(first);
    EXPECT_EQ(product.data, source);
    EXPECT_EQ(product.product_hash, kpengine::asset::ComputeNativeModelProductHash(first));
    EXPECT_EQ(product.integrity_digest,
              kpengine::asset::Sha256([&]
                                       {
                                           std::vector<std::byte> digest_input = first;
                                           std::fill(digest_input.begin() + kpengine::asset::kNativeModelDigestOffset,
                                                     digest_input.begin() + kpengine::asset::kNativeModelDigestOffset +
                                                         kpengine::asset::kNativeModelDigestSize,
                                                     std::byte{0});
                                           return digest_input;
                                       }()));
}

TEST(NativeModelFormatTest, RejectsTamperingAndUnsupportedValues)
{
    std::vector<std::byte> bytes = SerializeNativeModel(MakeModel());
    bytes.back() ^= std::byte{1};
    EXPECT_EQ(CatchNativeModelError(bytes), NativeModelErrorCode::IntegrityMismatch);

    bytes = SerializeNativeModel(MakeModel());
    bytes[8] = std::byte{3};
    EXPECT_EQ(CatchNativeModelError(bytes), NativeModelErrorCode::UnsupportedVersion);
}

TEST(NativeModelFormatTest, RejectsTruncatedAndInvalidChunkBounds)
{
    std::vector<std::byte> bytes = SerializeNativeModel(MakeModel());
    bytes.resize(bytes.size() - 1);
    EXPECT_EQ(CatchNativeModelError(bytes), NativeModelErrorCode::InvalidChunkTable);

    bytes = SerializeNativeModel(MakeModel());
    bytes[32] = std::byte{4};
    EXPECT_EQ(CatchNativeModelError(bytes), NativeModelErrorCode::InvalidChunkTable);
}

TEST(NativeModelLoaderTest, VerifiesArchiveHashAndCanonicalLayoutBeforeParsing)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                        "kpengine_native_model_archive_validation_test";
    std::filesystem::remove_all(root);
    const std::filesystem::path archive = root / ".archive";
    const std::vector<std::byte> bytes = SerializeNativeModel(MakeModelWithoutMaterials());
    const ContentHash hash = kpengine::asset::Sha256(bytes);
    const std::filesystem::path valid_path =
        archive / ProductRelativePath(ArchiveProductType::Model, hash);
    WriteBytes(valid_path, bytes);

    kpengine::asset::NativeModelLoader loader;
    kpengine::asset::AssetRegisterInfo info;
    ASSERT_TRUE(loader.Load(valid_path.string(), kpengine::asset::ModelGeometryType::KPMG_Mesh,
                            info));

    const std::filesystem::path mismatched_path =
        archive / "models" / (kpengine::asset::Sha256("different-product").ToHex() + ".model");
    WriteBytes(mismatched_path, bytes);
    info = {};
    EXPECT_FALSE(loader.Load(mismatched_path.string(),
                             kpengine::asset::ModelGeometryType::KPMG_Mesh, info));
    const std::filesystem::path runtime_mismatched_path =
        std::filesystem::path(kpengine::GetAssetDirectory()) / ".archive" / "models" /
        (kpengine::asset::Sha256("runtime-different-product").ToHex() + ".model");
    WriteBytes(runtime_mismatched_path, bytes);
    EXPECT_FALSE(kpengine::asset::AssetManager::GetInstance()
                     .LoadSync(runtime_mismatched_path.string())
                     .IsValid());
    std::filesystem::remove(runtime_mismatched_path);

    const std::filesystem::path wrong_layout_path =
        archive / "not-models" / (hash.ToHex() + ".model");
    WriteBytes(wrong_layout_path, bytes);
    info = {};
    EXPECT_FALSE(loader.Load(wrong_layout_path.string(),
                             kpengine::asset::ModelGeometryType::KPMG_Mesh, info));
    std::filesystem::remove_all(root);
}

TEST(NativeModelLoaderTest, DeclaresMaterialDependenciesFromHashReferences)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                        "kpengine_native_model_loader_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "models");
    const std::filesystem::path path = root / "models" / "triangle.model";
    const std::vector<std::byte> bytes = SerializeNativeModel(MakeModel());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();

    kpengine::asset::NativeModelLoader loader;
    kpengine::asset::AssetRegisterInfo info;
    ASSERT_TRUE(loader.Load(path.string(), kpengine::asset::ModelGeometryType::KPMG_Mesh, info));
    ASSERT_EQ(info.dependency_requests.size(), 1u);
    EXPECT_EQ(info.dependency_requests[0].expected_type, AssetType::KPAT_Material);
    EXPECT_EQ(std::filesystem::path(info.dependency_requests[0].path).generic_string(),
              (root / "materials" / (kpengine::asset::Sha256("material-slot-0").ToHex() + ".material"))
                  .generic_string());
    EXPECT_TRUE(info.dependencies.empty());
    ASSERT_EQ(info.owned_children.size(), 1u);
    EXPECT_EQ(info.owned_children[0].type, AssetType::KPAT_Mesh);
    EXPECT_NE(std::dynamic_pointer_cast<kpengine::asset::MeshResource>(
                  info.owned_children[0].resource),
              nullptr);
    EXPECT_EQ(info.type, AssetType::KPAT_Model);

    const auto model = std::dynamic_pointer_cast<kpengine::asset::ModelResource>(info.resource);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->GetMaterialDependencyIndices(), std::vector<std::uint32_t>{1});
    std::filesystem::remove_all(root);
}

TEST(NativeModelLoaderTest, DoesNotRegisterInlineMeshDuringDeclaration)
{
    const std::vector<std::byte> bytes = SerializeNativeModel(MakeModel());
    const std::filesystem::path path = ArchiveModelPath(bytes);
    std::filesystem::remove(path);
    WriteBytes(path, bytes);

    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const std::size_t mesh_count = assets.GetLiveAssetCount(AssetType::KPAT_Mesh);
    kpengine::asset::NativeModelLoader loader;
    kpengine::asset::AssetRegisterInfo info;
    ASSERT_TRUE(loader.Load(path.string(), kpengine::asset::ModelGeometryType::KPMG_Mesh, info));
    EXPECT_EQ(assets.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count);
    EXPECT_TRUE(info.dependencies.empty());
    ASSERT_EQ(info.owned_children.size(), 1u);
    std::filesystem::remove(path);
}

TEST(NativeModelLoaderTest, MissingMaterialRollsBackInlineMesh)
{
    const std::vector<std::byte> bytes = SerializeNativeModel(MakeModel());
    const std::filesystem::path path = ArchiveModelPath(bytes);
    std::filesystem::remove(path);
    WriteBytes(path, bytes);

    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const std::size_t mesh_count = assets.GetLiveAssetCount(AssetType::KPAT_Mesh);
    EXPECT_FALSE(assets.LoadSync(path.string()).IsValid());
    EXPECT_EQ(assets.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count);
    std::filesystem::remove(path);
}

TEST(NativeModelLoaderTest, LaterMaterialFailureKeepsEarlierSharedMaterial)
{
    const std::filesystem::path root =
        std::filesystem::path(kpengine::GetAssetDirectory()) / ".archive";
    const std::string material_text = ValidTestMaterial();
    const std::vector<std::byte> material_bytes(
        reinterpret_cast<const std::byte *>(material_text.data()),
        reinterpret_cast<const std::byte *>(material_text.data() + material_text.size()));
    const ContentHash first_hash = kpengine::asset::Sha256(material_bytes);
    const ContentHash second_hash = kpengine::asset::Sha256("native-missing-material");
    const std::filesystem::path first_material =
        root / "materials" / (first_hash.ToHex() + ".material");
    std::filesystem::remove(first_material);
    std::filesystem::create_directories(first_material.parent_path());
    std::ofstream material_file(first_material, std::ios::binary);
    material_file.write(reinterpret_cast<const char *>(material_bytes.data()),
                        static_cast<std::streamsize>(material_bytes.size()));
    material_file.close();

    NativeModelData model = MakeModelWithoutMaterials();
    model.material_references = {{AssetType::KPAT_Material, first_hash},
                                 {AssetType::KPAT_Material, second_hash}};
    const std::vector<std::byte> model_bytes = SerializeNativeModel(model);
    const std::filesystem::path model_path = ArchiveModelPath(model_bytes);
    std::filesystem::remove(model_path);
    WriteBytes(model_path, model_bytes);

    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const std::size_t mesh_count = assets.GetLiveAssetCount(AssetType::KPAT_Mesh);
    EXPECT_FALSE(assets.LoadSync(model_path.string()).IsValid());
    EXPECT_EQ(assets.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count);
    const AssetID first_material_id = assets.LoadSync(first_material.string());
    EXPECT_TRUE(first_material_id.IsValid());
    assets.UnRegisterAsset(first_material_id);
    std::filesystem::remove(first_material);
    std::filesystem::remove(model_path);
}

TEST(NativeModelLoaderTest, ConcurrentLoadsShareModelAndUnloadOwnedMesh)
{
    const std::vector<std::byte> bytes = SerializeNativeModel(MakeModelWithoutMaterials());
    const std::filesystem::path path = ArchiveModelPath(bytes);
    std::filesystem::remove(path);
    WriteBytes(path, bytes);

    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const std::size_t mesh_count = assets.GetLiveAssetCount(AssetType::KPAT_Mesh);
    auto first = std::async(std::launch::async, [&assets, path]()
                            { return assets.LoadSync(path.string()); });
    auto second = std::async(std::launch::async, [&assets, path]()
                             { return assets.LoadSync(path.string()); });
    const AssetID first_id = first.get();
    const AssetID second_id = second.get();
    ASSERT_TRUE(first_id.IsValid());
    EXPECT_EQ(second_id, first_id);
    EXPECT_EQ(assets.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count + 1);

    const AssetID mesh_id = assets.ResolveDependency(first_id, 0, AssetType::KPAT_Mesh);
    ASSERT_TRUE(mesh_id.IsValid());
    const auto *model_asset = assets.GetAsset(first_id);
    ASSERT_NE(model_asset, nullptr);
    ASSERT_EQ(model_asset->GetOwnedChildren().size(), 1u);
    EXPECT_EQ(model_asset->GetOwnedChildren().front(), mesh_id);

    assets.UnRegisterAsset(first_id);
    EXPECT_EQ(assets.GetAsset(mesh_id), nullptr);
    EXPECT_EQ(assets.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count);
    std::filesystem::remove(path);
}

TEST(NativeModelLoaderTest, ParentBindingFailureRollsBackOwnedChildren)
{
    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const std::size_t mesh_count = assets.GetLiveAssetCount(AssetType::KPAT_Mesh);

    kpengine::asset::AssetRegisterInfo info{};
    info.resource = std::make_shared<kpengine::asset::ModelResource>();
    info.path = "native_model_parent_binding_failure.model";
    info.name = "NativeModelParentBindingFailure";
    info.type = AssetType::KPAT_Model;
    auto mesh = std::make_shared<kpengine::asset::MeshResource>();
    info.owned_children.push_back({mesh, "native_model_parent_binding_failure#mesh",
                                   "NativeModelParentBindingFailureMesh", {},
                                   AssetType::KPAT_Mesh});
    info.bind_owned_children = [](const std::vector<AssetID> &)
    { throw std::runtime_error("test parent binding failure"); };

    EXPECT_FALSE(assets.RegisterAsset(info).IsValid());
    EXPECT_EQ(assets.GetLiveAssetCount(AssetType::KPAT_Mesh), mesh_count);
}

TEST(NativeModelRuntimeIntegrationTest, LoadsMultiMaterialGraphThroughAssetManager)
{
    const std::filesystem::path fixture_root = KPENGINE_NATIVE_RUNTIME_FIXTURE_DIR;
    const std::string first_material_text =
        ReadText(fixture_root / "multi_material_0.material");
    const std::string second_material_text =
        ReadText(fixture_root / "multi_material_1.material");
    const std::string level_fixture = ReadText(fixture_root / "multi_material.level");
    ASSERT_FALSE(first_material_text.empty());
    ASSERT_FALSE(second_material_text.empty());
    ASSERT_NE(level_fixture.find("model/native_runtime/multi_material"), std::string::npos);

    const std::filesystem::path archive_root =
        std::filesystem::path(kpengine::GetAssetDirectory()) / ".archive";
    const std::filesystem::path material_root = archive_root / "materials";
    const std::filesystem::path model_root = archive_root / "models";
    const std::filesystem::path texture_path = WriteNativeRuntimeTexture();
    const std::vector<std::byte> first_material_bytes = BytesFromText(first_material_text);
    const std::vector<std::byte> second_material_bytes = BytesFromText(second_material_text);
    const ContentHash first_material_hash = kpengine::asset::Sha256(first_material_bytes);
    const ContentHash second_material_hash = kpengine::asset::Sha256(second_material_bytes);
    const std::filesystem::path first_material_path =
        material_root / (first_material_hash.ToHex() + ".material");
    const std::filesystem::path second_material_path =
        material_root / (second_material_hash.ToHex() + ".material");
    WriteBytes(first_material_path, first_material_bytes);
    WriteBytes(second_material_path, second_material_bytes);

    const std::vector<std::byte> model_bytes = SerializeNativeModel(
        MakeMultiMaterialModel(first_material_hash, second_material_hash));
    const std::filesystem::path model_path =
        model_root / (kpengine::asset::Sha256(model_bytes).ToHex() + ".model");
    WriteBytes(model_path, model_bytes);

    // Register the fixture's readable logical key exactly as the offline
    // importer would. The runtime must consume this metadata read-only; it
    // must not rediscover the source or write a replacement product.
    {
        kpengine::asset::ModelArchiveDatabase archive{archive_root / "archive.sqlite3"};
        kpengine::asset::SourceRecord source{};
        source.normalized_path = "model/native_runtime/multi_material.obj";
        source.path_hash = kpengine::asset::Sha256(source.normalized_path);
        source.display_name = "multi_material";
        source.package_hash = kpengine::asset::HashSourcePackage(
            {{source.normalized_path, kpengine::asset::Sha256("native-runtime-fixture")}});
        source.importer_id = "native-runtime-fixture";
        source.importer_version = 1;
        source.settings_hash = kpengine::asset::Sha256("native-runtime-fixture-settings");
        source.native_model_version = kpengine::asset::kNativeModelVersion;
        source.status = kpengine::asset::SourceImportStatus::Ready;

        const kpengine::asset::ProductRecord model_product{
            kpengine::asset::Sha256(model_bytes), kpengine::asset::ArchiveProductType::Model,
            kpengine::asset::ProductRelativePath(kpengine::asset::ArchiveProductType::Model,
                                                  kpengine::asset::Sha256(model_bytes)),
            static_cast<std::uint64_t>(model_bytes.size()), kpengine::asset::kNativeModelVersion};
        const kpengine::asset::ProductRecord first_material_product{
            first_material_hash, kpengine::asset::ArchiveProductType::Material,
            kpengine::asset::ProductRelativePath(kpengine::asset::ArchiveProductType::Material,
                                                  first_material_hash),
            static_cast<std::uint64_t>(first_material_bytes.size()), 1};
        const kpengine::asset::ProductRecord second_material_product{
            second_material_hash, kpengine::asset::ArchiveProductType::Material,
            kpengine::asset::ProductRelativePath(kpengine::asset::ArchiveProductType::Material,
                                                  second_material_hash),
            static_cast<std::uint64_t>(second_material_bytes.size()), 1};
        archive.ReplaceSource(
            source, {}, {model_product, first_material_product, second_material_product},
            {{model_product.content_hash, model_product.asset_type, 0, -1, "multi_material"},
             {first_material_hash, kpengine::asset::ArchiveProductType::Material, 1, 0,
              "multi_material_0"},
             {second_material_hash, kpengine::asset::ArchiveProductType::Material, 1, 1,
              "multi_material_1"}},
            {});
    }

    const std::filesystem::path level_path =
        std::filesystem::path(kpengine::GetAssetDirectory()) / ".test_native_runtime" /
        "multi_material.level";
    WriteText(level_path, level_fixture);
    const ContentHash archive_hash_before = kpengine::asset::Sha256File(
        archive_root / "archive.sqlite3");
    const ContentHash level_hash_before = kpengine::asset::Sha256File(level_path);

    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const AssetID model_id = assets.LoadSync(model_path.string());
    ASSERT_TRUE(model_id.IsValid());
    ASSERT_EQ(model_id.type, AssetType::KPAT_Model);

    const Asset *model_asset = assets.GetAsset(model_id);
    ASSERT_NE(model_asset, nullptr);
    ASSERT_EQ(model_asset->GetOwnedChildren().size(), 1u);
    ASSERT_EQ(model_asset->GetDependencies().size(), 3u);
    const AssetID mesh_id = assets.ResolveDependency(model_id, 0, AssetType::KPAT_Mesh);
    const AssetID first_material_id = assets.ResolveDependency(model_id, 1, AssetType::KPAT_Material);
    const AssetID second_material_id = assets.ResolveDependency(model_id, 2, AssetType::KPAT_Material);
    ASSERT_TRUE(mesh_id.IsValid());
    ASSERT_TRUE(first_material_id.IsValid());
    ASSERT_TRUE(second_material_id.IsValid());
    EXPECT_EQ(model_asset->GetOwnedChildren().front(), mesh_id);

    const AssetID first_shader_id =
        assets.ResolveDependency(first_material_id, 0, AssetType::KPAT_ShaderProgram);
    const AssetID second_shader_id =
        assets.ResolveDependency(second_material_id, 0, AssetType::KPAT_ShaderProgram);
    const AssetID texture_id =
        assets.ResolveDependency(second_material_id, 1, AssetType::KPAT_Texture);
    ASSERT_TRUE(first_shader_id.IsValid());
    EXPECT_EQ(second_shader_id, first_shader_id);
    ASSERT_TRUE(texture_id.IsValid());

    const Asset *first_material_asset = assets.GetAsset(first_material_id);
    const Asset *second_material_asset = assets.GetAsset(second_material_id);
    const Asset *shader_asset = assets.GetAsset(first_shader_id);
    const Asset *texture_asset = assets.GetAsset(texture_id);
    ASSERT_NE(first_material_asset, nullptr);
    ASSERT_NE(second_material_asset, nullptr);
    ASSERT_NE(shader_asset, nullptr);
    ASSERT_NE(texture_asset, nullptr);
    const std::vector<AssetID> first_material_refs = first_material_asset->GetRefs();
    const std::vector<AssetID> second_material_refs = second_material_asset->GetRefs();
    const std::vector<AssetID> texture_refs = texture_asset->GetRefs();
    const std::vector<AssetID> shader_stage_ids = shader_asset->GetDependencies();
    EXPECT_NE(std::find(first_material_refs.begin(), first_material_refs.end(), model_id),
              first_material_refs.end());
    EXPECT_NE(std::find(second_material_refs.begin(), second_material_refs.end(), model_id),
              second_material_refs.end());
    EXPECT_GE(shader_asset->GetRefs().size(), 2u);
    EXPECT_NE(std::find(texture_refs.begin(), texture_refs.end(), second_material_id),
              texture_refs.end());

    // This is the migration seam: the Level contains only the readable
    // logical model key and no authored fallback material. LevelLoader must
    // resolve the key through the archive, while LevelInstance's later
    // selection policy receives the native Model's ordered material slots.
    const AssetID level_id = assets.LoadSync(level_path.string());
    ASSERT_TRUE(level_id.IsValid());
    ASSERT_EQ(level_id.type, AssetType::KPAT_Level);
    const Asset *level_asset = assets.GetAsset(level_id);
    ASSERT_NE(level_asset, nullptr);
    ASSERT_EQ(level_asset->GetDependencies().size(), 2u);
    EXPECT_EQ(assets.ResolveDependency(level_id, 0, AssetType::KPAT_Model), model_id);
    const AssetID error_material_id =
        assets.ResolveDependency(level_id, 1, AssetType::KPAT_Material);
    ASSERT_TRUE(error_material_id.IsValid());
    EXPECT_EQ(level_asset->GetPath(), level_path.string());
    EXPECT_EQ(kpengine::asset::Sha256File(archive_root / "archive.sqlite3"), archive_hash_before);
    EXPECT_EQ(kpengine::asset::Sha256File(level_path), level_hash_before);

    assets.UnRegisterAsset(level_id);

    auto concurrent_first = std::async(std::launch::async, [&assets, model_path]
                                       { return assets.LoadSync(model_path.string()); });
    auto concurrent_second = std::async(std::launch::async, [&assets, model_path]
                                        { return assets.LoadSync(model_path.string()); });
    EXPECT_EQ(concurrent_first.get(), model_id);
    EXPECT_EQ(concurrent_second.get(), model_id);

    assets.UnRegisterAsset(model_id);
    EXPECT_EQ(assets.GetAsset(model_id), nullptr);
    EXPECT_EQ(assets.GetAsset(mesh_id), nullptr);
    EXPECT_NE(assets.GetAsset(first_material_id), nullptr);
    EXPECT_NE(assets.GetAsset(second_material_id), nullptr);

    assets.UnRegisterAsset(first_material_id);
    assets.UnRegisterAsset(second_material_id);
    assets.UnRegisterAsset(first_shader_id);
    for (const AssetID &shader_stage_id : shader_stage_ids)
    {
        assets.UnRegisterAsset(shader_stage_id);
    }
    assets.UnRegisterAsset(texture_id);

    std::error_code error;
    std::filesystem::remove(model_path, error);
    std::filesystem::remove(first_material_path, error);
    std::filesystem::remove(second_material_path, error);
    std::filesystem::remove(texture_path, error);
    std::filesystem::remove(texture_path.parent_path(), error);
    std::filesystem::remove(level_path, error);
    {
        kpengine::asset::ModelArchiveDatabase archive{archive_root / "archive.sqlite3"};
        archive.RemoveSource("model/native_runtime/multi_material.obj");
    }
}
