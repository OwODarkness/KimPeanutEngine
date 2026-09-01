#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/level_loader.h"
#include "config/path.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::LevelLoader;
    using kpengine::asset::LevelPtr;

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
                              const std::string &environment = {})
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
        const std::string environment_field = environment.empty()
                                                   ? ""
                                                   : ",\n        \"environment\": {\"texture\": \"" +
                                                         escaped_environment + "\", \"ibl_intensity\": 0.25}";
        return "{\n"
               "  \"version\": 1,\n"
               "  \"objects\": [\n"
               "    {\"id\": \"mesh\", \"name\": \"Mesh\", \"kind\": \"static_mesh\","
               "     \"transform\": {\"position\": [0, 1, 2], \"rotation_degrees\": [0, 10, 0], \"scale\": [1, 2, 1]},"
               "     \"model\": \"" + escaped_model + "\", \"material\": \"" + escaped_material + "\", \"lod_bias\": 2},\n"
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
    const auto level = std::get<LevelPtr>(info.resource);
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

TEST(LevelLoaderTest, NormalizesSafeReferencesAndRejectsRootEscapeOrTypeMismatch)
{
    LevelFixture fixture;
    const std::string model = fixture.Relative("mesh.obj");
    const std::string material = fixture.Relative("surface.material");
    fixture.Write("scene.level", CompleteLevel(model + "\\./../" + fixture.Path("mesh.obj").filename().string(),
                                                 material));

    AssetRegisterInfo info{};
    ASSERT_TRUE(ParseDirect(fixture.Path("scene.level"), info));
    const auto level = std::get<LevelPtr>(info.resource);
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
