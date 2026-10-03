#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "module/tts/editor/tts_editor_settings.h"

namespace
{
    struct SettingsFixture
    {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("kp-tts-editor-settings-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::path path = directory / "settings.json";

        ~SettingsFixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(directory / "settings.json.tmp", error);
            std::filesystem::remove(directory, error);
        }
    };
}

TEST(TtsEditorSettingsTest, MissingFileCreatesBlankMachineSpecificTemplate)
{
    SettingsFixture fixture;
    kpengine::tts_editor::TtsEditorSettings settings;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::tts_editor::LoadOrCreateSettings(
        fixture.path, settings, diagnostic)) << diagnostic;
    EXPECT_TRUE(std::filesystem::is_regular_file(fixture.path));
    EXPECT_TRUE(settings.address.empty());
    EXPECT_EQ(settings.port, 0);
    ASSERT_NE(kpengine::tts_editor::SelectedVoice(settings), nullptr);
    EXPECT_TRUE(kpengine::tts_editor::SelectedVoice(settings)->ref_audio_path.empty());
    EXPECT_TRUE(kpengine::tts_editor::SelectedVoice(settings)->ref_text.empty());
    EXPECT_TRUE(settings.streaming);
    EXPECT_FALSE(kpengine::tts_editor::ValidateForGeneration(settings, diagnostic));
    EXPECT_NE(diagnostic.find("address"), std::string::npos);
}

TEST(TtsEditorSettingsTest, InvalidJsonIsPreservedForRepair)
{
    SettingsFixture fixture;
    std::filesystem::create_directories(fixture.directory);
    {
        std::ofstream output(fixture.path);
        output << "{ broken";
    }
    kpengine::tts_editor::TtsEditorSettings settings;
    std::string diagnostic;
    EXPECT_FALSE(kpengine::tts_editor::LoadOrCreateSettings(
        fixture.path, settings, diagnostic));
    std::ifstream input(fixture.path);
    std::string original;
    std::getline(input, original);
    EXPECT_EQ(original, "{ broken");
}

TEST(TtsEditorSettingsTest, RoundTripsConnectionReferenceAndPlaybackMode)
{
    SettingsFixture fixture;
    kpengine::tts_editor::TtsEditorSettings saved;
    saved.address = "localhost";
    saved.port = 9880;
    saved.voices[0].ref_audio_path = "server/voice.wav";
    saved.voices[0].ref_text = "reference sentence";
    saved.voices[0].ref_language = "en";
    saved.voices.push_back({"voice-2", "Second voice", "server/second.wav",
                            "another sentence", "en"});
    saved.selected_voice_id = "voice-2";
    saved.text_language = "en";
    saved.streaming = false;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::tts_editor::SaveSettings(fixture.path, saved, diagnostic))
        << diagnostic;
    kpengine::tts_editor::TtsEditorSettings loaded;
    ASSERT_TRUE(kpengine::tts_editor::LoadOrCreateSettings(
        fixture.path, loaded, diagnostic)) << diagnostic;
    EXPECT_EQ(loaded.address, saved.address);
    EXPECT_EQ(loaded.port, saved.port);
    ASSERT_EQ(loaded.voices.size(), 2u);
    EXPECT_EQ(loaded.selected_voice_id, "voice-2");
    ASSERT_NE(kpengine::tts_editor::SelectedVoice(loaded), nullptr);
    EXPECT_EQ(kpengine::tts_editor::SelectedVoice(loaded)->ref_audio_path,
              "server/second.wav");
    EXPECT_EQ(kpengine::tts_editor::SelectedVoice(loaded)->ref_text,
              "another sentence");
    EXPECT_FALSE(loaded.streaming);
    EXPECT_TRUE(kpengine::tts_editor::ValidateForGeneration(loaded, diagnostic))
        << diagnostic;
}

TEST(TtsEditorSettingsTest, RejectsDuplicateVoiceWithoutReplacingSavedFile)
{
    SettingsFixture fixture;
    kpengine::tts_editor::TtsEditorSettings valid;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::tts_editor::SaveSettings(fixture.path, valid, diagnostic));

    auto invalid = valid;
    invalid.voices.push_back(invalid.voices.front());
    EXPECT_FALSE(kpengine::tts_editor::SaveSettings(fixture.path, invalid, diagnostic));
    EXPECT_NE(diagnostic.find("Voice"), std::string::npos);

    kpengine::tts_editor::TtsEditorSettings loaded;
    ASSERT_TRUE(kpengine::tts_editor::LoadOrCreateSettings(
        fixture.path, loaded, diagnostic)) << diagnostic;
    ASSERT_EQ(loaded.voices.size(), 1u);
    EXPECT_EQ(loaded.selected_voice_id, "default");
}
