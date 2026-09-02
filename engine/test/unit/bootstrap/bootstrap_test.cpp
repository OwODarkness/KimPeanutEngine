#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bootstrap/bootstrap.h"

namespace
{
    const std::filesystem::path &BootstrapTestPath()
    {
        static const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "kpengine_bootstrap_test.json";
        return path;
    }

    void WriteTestFile(const std::string &content)
    {
        std::ofstream file(BootstrapTestPath());
        ASSERT_TRUE(file.is_open()) << BootstrapTestPath().string();
        file << content;
    }

    void RemoveTestFile()
    {
        std::error_code error;
        std::filesystem::remove(BootstrapTestPath(), error);
    }
}

TEST(BootstrapTest, ParsesNormalizedStartupLevel)
{
    WriteTestFile(R"({
        "version": 2,
        "startup_level": "level/./pbr_showcase.level"
    })");

    const auto config = kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string());
    EXPECT_EQ(config.version, 2);
    EXPECT_EQ(config.startup_level, "level/pbr_showcase.level");
    RemoveTestFile();
}

TEST(BootstrapTest, RejectsLegacyManifestAndScene)
{
    WriteTestFile(R"({
        "version": 1,
        "assets": [],
        "scene": {}
    })");
    EXPECT_THROW(kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string()),
                 std::runtime_error);
    RemoveTestFile();
}

TEST(BootstrapTest, RejectsInvalidStartupLevelPaths)
{
    const std::vector<std::string> invalid_documents{
        R"({"version":2})",
        R"({"version":2,"startup_level":""})",
        R"({"version":2,"startup_level":"../pbr.level"})",
        R"({"version":2,"startup_level":"C:/pbr.level"})",
        R"({"version":2,"startup_level":"material/pbr.material"})",
        R"({"version":2,"startup_level":"level/pbr.json"})",
        R"({"version":2,"startup_level":"level/pbr.level","extra":true})",
    };
    for (const std::string &document : invalid_documents)
    {
        WriteTestFile(document);
        EXPECT_THROW(kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string()),
                     std::runtime_error);
    }
    RemoveTestFile();
}

TEST(BootstrapTest, ThrowsOnMissingOrMalformedFile)
{
    RemoveTestFile();
    EXPECT_THROW(kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string()),
                 std::runtime_error);

    WriteTestFile("{ not valid json ]");
    EXPECT_THROW(kpengine::bootstrap::ReadBootstrap(BootstrapTestPath().string()),
                 std::exception);
    RemoveTestFile();
}
