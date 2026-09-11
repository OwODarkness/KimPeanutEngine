#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "live2d_settings.h"

namespace
{
    class Live2DSettingsFixture : public ::testing::Test
    {
    protected:
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / "kpengine_live2d_settings_test.json";

        void SetUp() override
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }

        void Write(const std::string &document)
        {
            std::ofstream file(path);
            ASSERT_TRUE(file.is_open());
            file << document;
        }
    };
}

TEST_F(Live2DSettingsFixture, NormalizesConfiguredNativeProduct)
{
    Write(R"({
        "version": 1,
        "enabled": true,
        "preview_asset": "live2d/./hiyori_pro/hiyori.live2d"
    })");

    const auto settings = kpengine::live2d::ReadLive2DSettings(path.string());
    EXPECT_EQ(settings.version, 1);
    EXPECT_TRUE(settings.enabled);
    EXPECT_EQ(settings.preview_asset, "live2d/hiyori_pro/hiyori.live2d");
}

TEST_F(Live2DSettingsFixture, AllowsDisabledPreviewWithoutAnAsset)
{
    Write(R"({"version":1,"enabled":false})");
    const auto settings = kpengine::live2d::ReadLive2DSettings(path.string());
    EXPECT_FALSE(settings.enabled);
    EXPECT_TRUE(settings.preview_asset.empty());
}

TEST_F(Live2DSettingsFixture, RejectsNonNativeOrEscapingAssetPaths)
{
    const std::string invalid_documents[] = {
        R"({"version":1,"enabled":true,"preview_asset":"live2d/hiyori.model3.json"})",
        R"({"version":1,"enabled":true,"preview_asset":"../hiyori.live2d"})",
        R"({"version":1,"enabled":true,"preview_asset":"C:/hiyori.live2d"})",
        R"({"version":1,"enabled":true,"preview_asset":"live2d/hiyori.live2d","extra":true})",
    };
    for (const std::string &document : invalid_documents)
    {
        Write(document);
        EXPECT_THROW(kpengine::live2d::ReadLive2DSettings(path.string()), std::runtime_error);
    }
}
