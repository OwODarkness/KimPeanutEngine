#ifndef KPENGINE_MODULE_TTS_EDITOR_SETTINGS_H
#define KPENGINE_MODULE_TTS_EDITOR_SETTINGS_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kpengine::tts_editor
{
    struct TtsVoicePreset
    {
        std::string id = "default";
        std::string name = "Default";
        std::string ref_audio_path;
        std::string ref_text;
        std::string ref_language;
    };

    struct TtsEditorSettings
    {
        std::string address;
        std::uint16_t port = 0;
        std::string api_path = "/tts";
        std::uint32_t timeout_seconds = 180;
        std::vector<TtsVoicePreset> voices{{}};
        std::string selected_voice_id = "default";
        std::string text_language;
        bool streaming = true;
        std::string output_directory = "tts/output";
    };

    std::filesystem::path TtsSettingsPath();
    bool LoadOrCreateSettings(const std::filesystem::path &path,
                              TtsEditorSettings &settings, std::string &diagnostic);
    bool SaveSettings(const std::filesystem::path &path,
                      const TtsEditorSettings &settings, std::string &diagnostic);
    bool ValidateSettings(const TtsEditorSettings &settings,
                          std::string &diagnostic);
    bool ValidateForGeneration(const TtsEditorSettings &settings,
                               std::string &diagnostic);
    const TtsVoicePreset *SelectedVoice(const TtsEditorSettings &settings);
}

#endif
