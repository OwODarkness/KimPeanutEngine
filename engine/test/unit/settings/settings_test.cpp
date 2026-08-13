#include "editor/settings/editor_settings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{
    using kpengine::editor::DefaultLogColors;
    using kpengine::editor::LogColor;
    using kpengine::editor::ReadEditorSettings;
    using kpengine::program::LogLevel;

    const std::filesystem::path &SettingsTestPath()
    {
        static std::filesystem::path path =
            std::filesystem::temp_directory_path() / "kpengine_settings_test.json";
        return path;
    }

    void WriteTestFile(const std::string &content)
    {
        std::ofstream file(SettingsTestPath());
        ASSERT_TRUE(file.is_open()) << "failed to write test file: " << SettingsTestPath().string();
        file << content;
    }

    void RemoveTestFile()
    {
        std::error_code ec;
        std::filesystem::remove(SettingsTestPath(), ec);
    }

    const LogColor &Color(const kpengine::editor::EditorSettings &settings, LogLevel level)
    {
        return settings.log_colors[static_cast<size_t>(level)];
    }
}

TEST(SettingsTest, ParsesVersionAndLogColors)
{
    WriteTestFile(R"({
        "version": 1,
        "log_colors": {
            "debug":   [0.1, 0.2, 0.3, 0.4],
            "info":    [0.5, 0.5, 0.5, 1.0],
            "warning": [1.0, 1.0, 0.0, 1.0],
            "error":   [0.5, 0.2, 0.0, 1.0],
            "fatal":   [1.0, 0.0, 0.0, 1.0]
        }
    })");
    const auto settings = ReadEditorSettings(SettingsTestPath().string());
    EXPECT_EQ(settings.version, 1);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Debug).r, 0.1f);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Debug).g, 0.2f);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Debug).b, 0.3f);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Debug).a, 0.4f);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Fatal).r, 1.0f);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Fatal).b, 0.0f);
    RemoveTestFile();
}

TEST(SettingsTest, MissingLevelFallsBackToDefault)
{
    WriteTestFile(R"({
        "log_colors": {
            "error": [1.0, 0.0, 0.0, 1.0]
        }
    })");
    const auto settings = ReadEditorSettings(SettingsTestPath().string());
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Error).r, 1.0f);
    const auto defaults = DefaultLogColors();
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Warning).r, defaults[static_cast<size_t>(LogLevel::Warning)].r);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Warning).g, defaults[static_cast<size_t>(LogLevel::Warning)].g);
    RemoveTestFile();
}

TEST(SettingsTest, MissingLogColorsUsesDefaults)
{
    WriteTestFile(R"({ "version": 1 })");
    const auto settings = ReadEditorSettings(SettingsTestPath().string());
    const auto defaults = DefaultLogColors();
    for (size_t i = 0; i < defaults.size(); ++i)
    {
        EXPECT_FLOAT_EQ(settings.log_colors[i].r, defaults[i].r);
        EXPECT_FLOAT_EQ(settings.log_colors[i].g, defaults[i].g);
        EXPECT_FLOAT_EQ(settings.log_colors[i].b, defaults[i].b);
        EXPECT_FLOAT_EQ(settings.log_colors[i].a, defaults[i].a);
    }
    RemoveTestFile();
}

TEST(SettingsTest, MalformedColorFallsBackToDefault)
{
    WriteTestFile(R"({
        "log_colors": {
            "warning": [1.0, 1.0],
            "error": ["red", 0, 0, 1]
        }
    })");
    const auto settings = ReadEditorSettings(SettingsTestPath().string());
    const auto defaults = DefaultLogColors();
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Warning).r, defaults[static_cast<size_t>(LogLevel::Warning)].r);
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Error).r, defaults[static_cast<size_t>(LogLevel::Error)].r);
    RemoveTestFile();
}

TEST(SettingsTest, IgnoresUnknownLevelWithDefaults)
{
    WriteTestFile(R"({
        "log_colors": {
            "fatal": [0.0, 1.0, 0.0, 1.0],
            "verbose": [0.1, 0.2, 0.3, 0.4]
        }
    })");
    const auto settings = ReadEditorSettings(SettingsTestPath().string());
    EXPECT_FLOAT_EQ(Color(settings, LogLevel::Fatal).g, 1.0f);
    // Unknown "verbose" is ignored; every known level keeps a value.
    const auto defaults = DefaultLogColors();
    for (size_t i = 0; i < defaults.size(); ++i)
    {
        EXPECT_FLOAT_EQ(settings.log_colors[i].b, defaults[i].b);
    }
    RemoveTestFile();
}

TEST(SettingsTest, MissingFileThrows)
{
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "kpengine_settings_does_not_exist.json";
    EXPECT_THROW(ReadEditorSettings(missing.string()), std::runtime_error);
}

TEST(SettingsTest, ThrowsOnMalformedJson)
{
    WriteTestFile("{ not valid json ]");
    EXPECT_THROW(ReadEditorSettings(SettingsTestPath().string()), std::exception);
    RemoveTestFile();
}
