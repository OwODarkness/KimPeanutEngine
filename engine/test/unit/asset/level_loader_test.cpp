#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/level_loader.h"
#include "asset/model_archive.h"
#include "asset/native_model.h"
#include "config/path.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::LevelLoader;
    using kpengine::asset::LevelPtr;
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::HashSourcePackage;
    using kpengine::asset::ModelArchiveDatabase;
    using kpengine::asset::NativeModelData;
    using kpengine::asset::ProductRecord;
    using kpengine::asset::ProductRelativePath;
    using kpengine::asset::SerializeNativeModel;
    using kpengine::asset::Sha256;
    using kpengine::asset::SourceImportStatus;
    using kpengine::asset::SourceProductRecord;
    using kpengine::asset::SourceRecord;

    std::atomic_uint32_t fixture_number{0};

    class LevelFixture
    {
    public:
        LevelFixture()
        {
            root_ = std::filesystem::path(kpengine::GetAssetDirectory()) /
                    ("gp71_test_" + std::to_string(fixture_number.fetch_add(1)));
            std::filesystem::create_directories(root_);
        }

        ~LevelFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        std::filesystem::path Path(const char *name) const { return root_ / name; }

        std::string Relative(const char *name) const
        {
            return std::filesystem::relative(Path(name),
                                              std::filesystem::path(kpengine::GetAssetDirectory()))
                .generic_string();
        }

        void Write(const char *name, const std::string &contents) const
        {
            std::ofstream file(Path(name));
            ASSERT_TRUE(file.is_open()) << Path(name).string();
            file << contents;
        }

        void WriteDependencies() const
        {
            Write("mesh.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            Write("not_loaded.shader", R"({"version":1,"shaders":[]})");
            Write("surface.material", R"({
                "version": 1,
                "shader": "not_loaded.shader",
                "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
                "parameters": {}
            })");
        }

    private:
        std::filesystem::path root_;
    };

    std::string CompleteLevel(const std::string &model, const std::string &material,
                              const std::string &environment = {},
                              const std::vector<std::string> &section_materials = {})
    {
        const auto JsonEscape = [](const std::string &value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value)
            {
                if (character == '\\')
                {
                    escaped += "\\\\";
                }
                else if (character == '"')
                {
                    escaped += "\\\"";
                }
                else
                {
                    escaped += character;
                }
            }
            return escaped;
        };
        const std::string escaped_model = JsonEscape(model);
        const std::string escaped_material = JsonEscape(material);
        const std::string escaped_environment = JsonEscape(environment);
        std::string section_materials_field;
        if (!section_materials.empty())
        {
            section_materials_field = ",\n        \"materials\": [";
            for (std::size_t index = 0; index < section_materials.size(); ++index)
            {
                if (index != 0)
                {
                    section_materials_field += ", ";
                }
                section_materials_field += "\"" + JsonEscape(section_materials[index]) + "\"";
            }
            section_materials_field += "]";
        }
        const std::string environment_field = environment.empty()
                                                   ? ""
                                                   : ",\n        \"environment\": {\"texture\": \"" +
                                                         escaped_environment + "\", \"ibl_intensity\": 0.25}";
        return "{\n"
               "  \"version\": 1,\n"
               "  \"objects\": [\n"
               "    {\"id\": \"mesh\", \"name\": \"Mesh\", \"kind\": \"static_mesh\","
               "     \"transform\": {\"position\": [0, 1, 2], \"rotation_degrees\": [0, 10, 0], \"scale\": [1, 2, 1]},"
               "     \"model\": \"" + escaped_model + "\", \"material\": \"" + escaped_material + "\"" +
                   section_materials_field + ", \"lod_bias\": 2},\n"
               "    {\"id\": \"mesh_copy\", \"kind\": \"static_mesh\","
               "     \"transform\": {\"position\": [0, 0, 0], \"rotation_degrees\": [0, 0, 0], \"scale\": [1, 1, 1]},"
               "     \"model\": \"" + escaped_model + "\", \"material\": \"" + escaped_material + "\"},\n"
               "    {\"id\": \"sun\", \"kind\": \"directional_light\", \"direction\": [1, -1, 0], \"color\": [1, 0.9, 0.8], \"intensity\": 2},\n"
               "    {\"id\": \"point\", \"kind\": \"point_light\", \"position\": [0, 2, 0], \"color\": [1, 1, 1], \"intensity\": 1, \"range\": 10},\n"
               "    {\"id\": \"spot\", \"kind\": \"spot_light\", \"position\": [0, 2, 0], \"direction\": [0, -1, 0], \"color\": [1, 1, 1], \"intensity\": 1, \"range\": 10, \"inner_cone_radians\": 0.1, \"outer_cone_radians\": 0.5},\n"
               "    {\"id\": \"camera\", \"kind\": \"camera\", \"transform\": {\"position\": [0, 0, 3], \"rotation_degrees\": [0, 0, 0], \"scale\": [1, 1, 1]}, \"projection\": \"perspective\", \"near_plane\": 0.1, \"far_plane\": 100}\n"
               "  ]" + environment_field + "\n"
               "}\n";
    }

    bool ParseDirect(const std::filesystem::path &path, AssetRegisterInfo &info)
    {
        LevelLoader loader;
        return loader.Load(path.string(), info);
    }

}

TEST(LevelLoaderTest, LoadsCompleteV1RecordsAndDeduplicatesRequests)
{
    LevelFixture fixture;
    const std::string model = fixture.Relative("mesh.obj");
    const std::string material = fixture.Relative("surface.material");
    const std::string environment = fixture.Relative("sky.hdr");
    fixture.Write("scene.level", CompleteLevel(model, material, environment));

    AssetRegisterInfo info{};
    ASSERT_TRUE(ParseDirect(fixture.Path("scene.level"), info));
    ASSERT_EQ(info.type, AssetType::KPAT_Level);
    ASSERT_EQ(info.dependency_requests.size(), 3u);
    const auto level = std::dynamic_pointer_cast<kpengine::asset::LevelResource>(info.resource);
    ASSERT_NE(level, nullptr);
    ASSERT_EQ(level->objects.size(), 6u);
    ASSERT_TRUE(level->environment.has_value());
    const auto &mesh = std::get<kpengine::asset::LevelStaticMeshRecord>(level->objects[0]);
    EXPECT_EQ(mesh.model.dependency_index, 1u);
    EXPECT_EQ(mesh.material.dependency_index, 2u);
    EXPECT_EQ(mesh.lod_bias, 2);
    const auto &mesh_copy = std::get<kpengine::asset::LevelStaticMeshRecord>(level->objects[1]);
    EXPECT_EQ(mesh_copy.model.dependency_index, mesh.model.dependency_index);
    EXPECT_EQ(mesh_copy.material.dependency_index, mesh.material.dependency_index);
    EXPECT_EQ(level->environment->texture.dependency_index, 0u);
    EXPECT_EQ(mesh.model.path, model);
}

TEST(LevelLoaderTest, LoadsOptionalPerSectionMaterialReferences)
{
    LevelFixture fixture;
    const std::string model = fixture.Relative("mesh.obj");
    const std::string material = fixture.Relative("surface.material");
    const std::string section_material_a = fixture.Relative("surface_a.material");
    const std::string section_material_b = fixture.Relative("surface_b.material");
    fixture.Write("scene.level", CompleteLevel(model, material, {},
                                                {section_material_a, section_material_b}));

    AssetRegisterInfo info{};
    ASSERT_TRUE(ParseDirect(fixture.Path("scene.level"), info));
    ASSERT_EQ(info.dependency_requests.size(), 4u);
    const auto level = std::dynamic_pointer_cast<kpengine::asset::LevelResource>(info.resource);
    ASSERT_NE(level, nullptr);
    const auto &mesh = std::get<kpengine::asset::LevelStaticMeshRecord>(level->objects[0]);
    ASSERT_EQ(mesh.materials.size(), 2u);
    EXPECT_EQ(mesh.materials[0].dependency_index, 2u);
    EXPECT_EQ(mesh.materials[1].dependency_index, 3u);
    EXPECT_EQ(mesh.materials[0].path, section_material_a);
    EXPECT_EQ(mesh.materials[1].path, section_material_b);
}

TEST(LevelLoaderTest, NativeModelMayOmitMaterialAndGetsImplicitErrorFallback)
{
    LevelFixture fixture;
    fixture.Write("scene.level", R"({
      "version": 1,
      "objects": [{
        "id": "native_mesh",
        "kind": "static_mesh",
        "transform": {"position": [0, 0, 0], "rotation_degrees": [0, 0, 0], "scale": [1, 1, 1]},
        "model": "archive/models/0123456789abcdef0123456789abcdef.model"
      }]
    })");

    AssetRegisterInfo info{};
    ASSERT_TRUE(ParseDirect(fixture.Path("scene.level"), info));
    ASSERT_EQ(info.dependency_requests.size(), 2u);
    const auto level = std::dynamic_pointer_cast<kpengine::asset::LevelResource>(info.resource);
    ASSERT_NE(level, nullptr);
    const auto &mesh = std::get<kpengine::asset::LevelStaticMeshRecord>(level->objects.front());
    EXPECT_EQ(mesh.material.path, kpengine::asset::kEngineErrorMaterialAssetPath);
    EXPECT_EQ(mesh.material.dependency_index, 1u);
    EXPECT_EQ(info.dependency_requests[1].expected_type, AssetType::KPAT_Material);
}

TEST(LevelLoaderTest, LegacyModelStillRequiresAuthoredMaterial)
{
    LevelFixture fixture;
    fixture.Write("scene.level", R"({
      "version": 1,
      "objects": [{
        "id": "legacy_mesh",
        "kind": "static_mesh",
        "transform": {"position": [0, 0, 0], "rotation_degrees": [0, 0, 0], "scale": [1, 1, 1]},
        "model": "source/mesh.obj"
      }]
    })");

    AssetRegisterInfo info{};
    EXPECT_FALSE(ParseDirect(fixture.Path("scene.level"), info));
}

TEST(LevelLoaderTest, ResolvesReadableLogicalModelKeyThroughReadOnlyArchive)
{
    LevelFixture fixture;
    const std::string source_path = fixture.Relative("mesh.obj");
    const std::string logical_model = fixture.Relative("mesh");
    fixture.Write("scene.level", R"({
      "version": 1,
      "objects": [{
        "id": "native_mesh",
        "kind": "static_mesh",
        "transform": {"position": [0, 0, 0], "rotation_degrees": [0, 0, 0], "scale": [1, 1, 1]},
        "model": ")" + logical_model + R"("
      }]
    })");

    const std::vector<std::byte> model_bytes = SerializeNativeModel(NativeModelData{});
    const kpengine::asset::ContentHash model_hash = Sha256(model_bytes);
    const std::filesystem::path product_path =
        fixture.Path(".archive") / ProductRelativePath(ArchiveProductType::Model, model_hash);
    std::filesystem::create_directories(product_path.parent_path());
    std::ofstream product(product_path, std::ios::binary);
    ASSERT_TRUE(product.is_open());
    product.write(reinterpret_cast<const char *>(model_bytes.data()),
                  static_cast<std::streamsize>(model_bytes.size()));
    ASSERT_TRUE(product.good());
    product.close();

    ModelArchiveDatabase archive{fixture.Path(".archive") / "archive.sqlite3"};
    SourceRecord source;
    source.normalized_path = source_path;
    source.path_hash = Sha256(source_path);
    source.display_name = "mesh";
    source.importer_id = "test";
    source.importer_version = 1;
    source.settings_hash = Sha256("settings");
    source.native_model_version = 1;
    source.status = SourceImportStatus::Ready;
    const auto source_hash = Sha256("source");
    source.package_hash = HashSourcePackage({{source_path, source_hash}});

    const ProductRecord product_record{
        model_hash, ArchiveProductType::Model,
        ProductRelativePath(ArchiveProductType::Model, model_hash),
        static_cast<std::uint64_t>(model_bytes.size()), 1};
    const SourceProductRecord source_product{
        model_hash, ArchiveProductType::Model, 0, -1, "mesh"};
    archive.ReplaceSource(source, {{source_path, source_hash}}, {product_record},
                          {source_product}, {});

    LevelLoader loader(fixture.Path(".archive"));
    AssetRegisterInfo info{};
    ASSERT_TRUE(loader.Load(fixture.Path("scene.level").string(), info));
    const auto level = std::dynamic_pointer_cast<kpengine::asset::LevelResource>(info.resource);
    ASSERT_NE(level, nullptr);
    const auto &mesh = std::get<kpengine::asset::LevelStaticMeshRecord>(level->objects.front());
    EXPECT_EQ(mesh.model.path, logical_model);
    ASSERT_EQ(info.dependency_requests.size(), 2u);
    EXPECT_EQ(info.dependency_requests[0].expected_type, AssetType::KPAT_Model);
    EXPECT_EQ(info.dependency_requests[0].path, product_path.generic_string());
}

TEST(LevelLoaderTest, NormalizesSafeReferencesAndRejectsRootEscapeOrTypeMismatch)
{
    LevelFixture fixture;
    const std::string model = fixture.Relative("mesh.obj");
    const std::string material = fixture.Relative("surface.material");
    fixture.Write("scene.level", CompleteLevel(model + "\\./../" + fixture.Path("mesh.obj").filename().string(),
                                                 material));

    AssetRegisterInfo info{};
    ASSERT_TRUE(ParseDirect(fixture.Path("scene.level"), info));
    const auto level = std::dynamic_pointer_cast<kpengine::asset::LevelResource>(info.resource);
    EXPECT_EQ(std::get<kpengine::asset::LevelStaticMeshRecord>(level->objects[0]).model.path, model);

    const std::vector<std::string> invalid_references{
        "../outside.obj", "/root/model.obj", "C:/model.obj", "surface.material"};
    for (std::size_t index = 0; index < invalid_references.size(); ++index)
    {
        fixture.Write("invalid.level", CompleteLevel(invalid_references[index], material));
        AssetRegisterInfo invalid_info{};
        EXPECT_FALSE(ParseDirect(fixture.Path("invalid.level"), invalid_info)) << index;
    }
}

TEST(LevelLoaderTest, RejectsClosedSchemaAndInvalidValues)
{
    LevelFixture fixture;
    const std::string model = fixture.Relative("mesh.obj");
    const std::string material = fixture.Relative("surface.material");
    const std::vector<std::string> invalid_documents{
        "{\"version\":2,\"objects\":[]}",
        "{\"version\":1,\"objects\":[],\"typo\":true}",
        "{\"version\":1,\"objects\":[{\"id\":\"x\"}]}",
        "{\"version\":1,\"objects\":[{\"id\":\"x\",\"kind\":\"unknown\"}]}",
        "{\"version\":1,\"objects\":[{\"id\":\"x\",\"kind\":\"static_mesh\",\"transform\":{\"position\":[0,0,0],\"rotation_degrees\":[0,0,0],\"scale\":[0,1,1]},\"model\":\"" + model + "\",\"material\":\"" + material + "\"}]}",
        "{\"version\":1,\"objects\":[{\"id\":\"x\",\"kind\":\"directional_light\",\"direction\":[0,0,0],\"color\":[1,1,1],\"intensity\":1}]}",
        "{\"version\":1,\"objects\":[{\"id\":\"x\",\"kind\":\"spot_light\",\"position\":[0,0,0],\"direction\":[0,1,0],\"color\":[1,1,1],\"intensity\":1,\"range\":1,\"inner_cone_radians\":0.8,\"outer_cone_radians\":0.2}]}",
    };
    for (std::size_t index = 0; index < invalid_documents.size(); ++index)
    {
        fixture.Write("invalid.level", invalid_documents[index]);
        AssetRegisterInfo info{};
        EXPECT_FALSE(ParseDirect(fixture.Path("invalid.level"), info)) << index;
    }
}

TEST(LevelLoaderTest, LoadsDependencyGraphAndBlocksDependencyUnregister)
{
    LevelFixture fixture;
    fixture.WriteDependencies();
    fixture.Write("scene.level", CompleteLevel(fixture.Relative("mesh.obj"),
                                                 fixture.Relative("surface.material")));

    AssetManager &assets = AssetManager::GetInstance();
    const AssetID level_id = assets.LoadSync(fixture.Path("scene.level").string());
    ASSERT_TRUE(level_id.IsValid());
    EXPECT_EQ(level_id, assets.LoadSync(fixture.Path("scene.level").string()));
    const std::vector<AssetID> dependencies = assets.GetAsset(level_id)->GetDependencies();
    ASSERT_EQ(dependencies.size(), 2u);
    EXPECT_EQ(assets.ResolveDependency(level_id, 0, AssetType::KPAT_Model), dependencies[0]);
    EXPECT_EQ(assets.ResolveDependency(level_id, 1, AssetType::KPAT_Material), dependencies[1]);
    EXPECT_FALSE(assets.ResolveDependency(level_id, 1, AssetType::KPAT_Texture).IsValid());
    EXPECT_FALSE(assets.ResolveDependency(level_id, 2, AssetType::KPAT_Model).IsValid());

    assets.UnRegisterAsset(dependencies[0]);
    EXPECT_NE(assets.GetAsset(dependencies[0]), nullptr);
    assets.UnRegisterAsset(level_id);
    EXPECT_EQ(assets.GetAsset(level_id), nullptr);
    EXPECT_EQ(assets.ResolveDependency(level_id, 0, AssetType::KPAT_Model), AssetID());
    assets.UnRegisterAsset(dependencies[0]);
    assets.UnRegisterAsset(dependencies[1]);
}

TEST(LevelLoaderTest, FailedDependencyDoesNotRegisterParent)
{
    LevelFixture fixture;
    fixture.Write("mesh.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    fixture.Write("scene.level", CompleteLevel(fixture.Relative("mesh.obj"),
                                                 fixture.Relative("missing.material")));

    AssetManager &assets = AssetManager::GetInstance();
    EXPECT_FALSE(assets.LoadSync(fixture.Path("scene.level").string()).IsValid());
    const AssetID model_id = assets.LoadSync(fixture.Path("mesh.obj").string());
    EXPECT_TRUE(model_id.IsValid());
    EXPECT_TRUE(assets.GetAsset(model_id)->GetRefs().empty());
}

TEST(LevelLoaderTest, ConcurrentLoadsShareOneLevelIdentity)
{
    LevelFixture fixture;
    fixture.Write("scene.level", "{\"version\":1,\"objects\":[]}");
    std::vector<std::future<AssetID>> requests;
    for (int index = 0; index < 8; ++index)
    {
        requests.emplace_back(std::async(std::launch::async, [&fixture]()
                                         { return AssetManager::GetInstance().LoadSync(fixture.Path("scene.level").string()); }));
    }
    const AssetID expected = requests.front().get();
    ASSERT_TRUE(expected.IsValid());
    for (std::size_t index = 1; index < requests.size(); ++index)
    {
        EXPECT_EQ(requests[index].get(), expected);
    }
    AssetManager::GetInstance().UnRegisterAsset(expected);
}

TEST(LevelLoaderTest, LoadsCheckedInGameplayLevelFixtures)
{
    AssetManager &assets = AssetManager::GetInstance();
    const std::vector<std::filesystem::path> fixture_paths{
        std::filesystem::path(kpengine::GetAssetDirectory()) / "level" / "pbr_showcase.level",
        std::filesystem::path(kpengine::GetAssetDirectory()) / "level" / "point_shadow_validation.level",
        std::filesystem::path(kpengine::GetAssetDirectory()) / "level" / "spot_shadow_validation.level",
    };

    std::vector<AssetID> loaded_levels;
    for (const auto &path : fixture_paths)
    {
        const AssetID level_id = assets.LoadSync(path.string());
        ASSERT_TRUE(level_id.IsValid()) << path.string();
        ASSERT_EQ(level_id.type, AssetType::KPAT_Level);
        ASSERT_NE(assets.GetAsset(level_id), nullptr);
        loaded_levels.push_back(level_id);
    }

    for (const AssetID level_id : loaded_levels)
    {
        assets.UnRegisterAsset(level_id);
    }
}
