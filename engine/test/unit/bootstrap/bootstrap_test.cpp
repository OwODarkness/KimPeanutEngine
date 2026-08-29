#include "bootstrap/bootstrap.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "config/path.h"
#include <stdexcept>

namespace
{
    const std::filesystem::path &BootstrapTestPath()
    {
        static std::filesystem::path path =
            std::filesystem::temp_directory_path() / "kpengine_bootstrap_test.json";
        return path;
    }

    void WriteTestFile(const std::string &content)
    {
        std::ofstream file(BootstrapTestPath());
        ASSERT_TRUE(file.is_open()) << "failed to write test file: " << BootstrapTestPath().string();
        file << content;
    }

    void RemoveTestFile()
    {
        std::error_code ec;
        std::filesystem::remove(BootstrapTestPath(), ec);
    }
}

TEST(BootstrapTest, ParsesVersionAndAssets)
{
    WriteTestFile(R"({
        "version": 1,
        "assets": ["shader/a.shader", "shader/b.shader"]
    })");
    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    EXPECT_EQ(config.version, 1);
    ASSERT_EQ(config.assets.size(), 2u);
    EXPECT_EQ(config.assets[0], "shader/a.shader");
    EXPECT_EQ(config.assets[1], "shader/b.shader");
    RemoveTestFile();
}

TEST(BootstrapTest, ParsesSceneWithMaterialAsset)
{
    WriteTestFile(R"({
        "version": 1,
        "assets": ["material/bootstrap.material"],
        "scene": {
            "model": "model/sphere/sphere.obj",
            "material": "material/bootstrap.material"
        }
    })");

    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    EXPECT_TRUE(config.scene.IsComplete());
    EXPECT_EQ(config.scene.material, "material/bootstrap.material");
    RemoveTestFile();
}

TEST(BootstrapTest, ParsesAdditionalSceneObjectTransform)
{
    WriteTestFile(R"({
        "version": 1,
        "assets": [],
        "scene": {
            "model": "model/rock.obj",
            "material": "material/rock.material",
            "objects": [{
                "model": "model/floor.obj",
                "material": "material/brickwall.material",
                "position": [0, -70, 0],
                "rotation": [0, 0, 0],
                "scale": [30, 30, 30]
            }]
        }
    })");
    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    ASSERT_EQ(config.scene.objects.size(), 1u);
    EXPECT_EQ(config.scene.objects[0].model, "model/floor.obj");
    EXPECT_FLOAT_EQ(config.scene.objects[0].position[1], -70.f);
    EXPECT_FLOAT_EQ(config.scene.objects[0].scale[0], 30.f);
    RemoveTestFile();
}

TEST(BootstrapTest, DefaultsVersionWhenMissing)
{
    WriteTestFile(R"({
        "assets": ["shader/a.shader"]
    })");
    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    EXPECT_EQ(config.version, 1);
    ASSERT_EQ(config.assets.size(), 1u);
    EXPECT_EQ(config.assets[0], "shader/a.shader");
    RemoveTestFile();
}

TEST(BootstrapTest, EmptyAssetsIsValid)
{
    WriteTestFile(R"({ "version": 1, "assets": [] })");
    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    EXPECT_TRUE(config.assets.empty());
    RemoveTestFile();
}

TEST(BootstrapTest, MissingAssetsFieldIsValid)
{
    WriteTestFile(R"({ "version": 1 })");
    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    EXPECT_TRUE(config.assets.empty());
    RemoveTestFile();
}

TEST(BootstrapTest, IgnoresNonStringEntries)
{
    WriteTestFile(R"({
        "version": 1,
        "assets": ["shader/a.shader", 42, { "bad": true }]
    })");
    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    ASSERT_EQ(config.assets.size(), 1u);
    EXPECT_EQ(config.assets[0], "shader/a.shader");
    RemoveTestFile();
}

TEST(BootstrapTest, ThrowsOnMissingFile)
{
    RemoveTestFile();
    EXPECT_THROW(kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string()), std::runtime_error);
}

TEST(BootstrapTest, ThrowsOnMalformedJson)
{
    WriteTestFile("{ not valid json ]");
    EXPECT_THROW(kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string()), std::exception);
    RemoveTestFile();
}

TEST(BootstrapTest, BuildsQueuedRequestsFromConfig)
{
    kpengine::bootstrap::BootstrapConfig config;
    config.assets = {"shader/a.shader", "texture/albedo.png", "audio/sfx.wav"};

    const auto requests = kpengine::bootstrap::BuildLoadRequests(config);

    ASSERT_EQ(requests.size(), 3u);
    EXPECT_EQ(requests[0].path, kpengine::GetAssetDirectory() + "shader/a.shader");
    EXPECT_EQ(requests[0].type, kpengine::asset::AssetType::KPAT_ShaderProgram);
    EXPECT_EQ(requests[0].state, kpengine::asset::RequestState::Queued);
    EXPECT_EQ(requests[1].type, kpengine::asset::AssetType::KPAT_Texture);
    EXPECT_EQ(requests[2].type, kpengine::asset::AssetType::KPAT_Audio);
}

TEST(BootstrapTest, SkipsUnknownExtension)
{
    kpengine::bootstrap::BootstrapConfig config;
    config.assets = {"shader/a.shader", "data/unknown.bin"};

    const auto requests = kpengine::bootstrap::BuildLoadRequests(config);

    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].path, kpengine::GetAssetDirectory() + "shader/a.shader");
    EXPECT_EQ(requests[0].type, kpengine::asset::AssetType::KPAT_ShaderProgram);
}

TEST(BootstrapTest, EmptyConfigBuildsNoRequests)
{
    kpengine::bootstrap::BootstrapConfig config;
    EXPECT_TRUE(kpengine::bootstrap::BuildLoadRequests(config).empty());
}

TEST(BootstrapTest, DeduplicatesRepeatedPath)
{
    kpengine::bootstrap::BootstrapConfig config;
    config.assets = {"shader/a.shader", "shader/a.shader", "texture/t.png"};

    const auto requests = kpengine::bootstrap::BuildLoadRequests(config);

    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[0].path, kpengine::GetAssetDirectory() + "shader/a.shader");
    EXPECT_EQ(requests[1].path, kpengine::GetAssetDirectory() + "texture/t.png");
}

TEST(BootstrapTest, AssignsDistinctRequestIds)
{
    kpengine::bootstrap::BootstrapConfig config;
    config.assets = {"shader/a.shader", "shader/b.shader", "shader/c.shader"};

    const auto requests = kpengine::bootstrap::BuildLoadRequests(config);

    ASSERT_EQ(requests.size(), 3u);
    EXPECT_NE(requests[0].request_id, 0u);
    EXPECT_NE(requests[0].request_id, requests[1].request_id);
    EXPECT_NE(requests[1].request_id, requests[2].request_id);
    EXPECT_NE(requests[0].request_id, requests[2].request_id);
}
