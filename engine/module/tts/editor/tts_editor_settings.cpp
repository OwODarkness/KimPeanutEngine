#include "tts_editor_settings.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "runtime/core/config/path.h"

namespace kpengine::tts_editor
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::uintmax_t kMaximumSettingsBytes = 256u * 1024u;

        Json Serialize(const TtsEditorSettings &settings)
        {
            Json voices = Json::array();
            for (const auto &voice : settings.voices)
            {
                voices.push_back({{"id", voice.id}, {"name", voice.name},
                                  {"ref_audio_path", voice.ref_audio_path},
                                  {"ref_text", voice.ref_text},
                                  {"ref_language", voice.ref_language}});
            }
            return {
                {"schema_version", 1},
                {"server", {{"address", settings.address}, {"port", settings.port},
                            {"api_path", settings.api_path},
                            {"timeout_seconds", settings.timeout_seconds}}},
                {"voices", std::move(voices)},
                {"selected_voice_id", settings.selected_voice_id},
                {"text_language", settings.text_language},
                {"playback_mode", settings.streaming ? "stream" : "buffer"},
                {"output_directory", settings.output_directory}
            };
        }

        bool Parse(const Json &json, TtsEditorSettings &settings,
                   std::string &diagnostic)
        {
            try
            {
                if (!json.is_object() || json.at("schema_version").get<int>() != 1)
                    throw std::runtime_error("unsupported schema_version");
                const Json &server = json.at("server");
                const Json &voices = json.at("voices");
                if (!server.is_object() || !voices.is_array() ||
                    voices.empty() || voices.size() > 16)
                    throw std::runtime_error("server is invalid or voices must contain 1 to 16 presets");
                const auto port = server.at("port").get<std::uint32_t>();
                const auto timeout = server.at("timeout_seconds").get<std::uint32_t>();
                if (port > std::numeric_limits<std::uint16_t>::max() || timeout == 0)
                    throw std::runtime_error("port or timeout is outside its valid range");
                const auto mode = json.at("playback_mode").get<std::string>();
                if (mode != "stream" && mode != "buffer")
                    throw std::runtime_error("playback_mode must be stream or buffer");
                TtsEditorSettings parsed;
                parsed.address = server.at("address").get<std::string>();
                parsed.port = static_cast<std::uint16_t>(port);
                parsed.api_path = server.at("api_path").get<std::string>();
                parsed.timeout_seconds = timeout;
                parsed.voices.clear();
                for (const auto &raw_voice : voices)
                {
                    TtsVoicePreset voice;
                    voice.id = raw_voice.at("id").get<std::string>();
                    voice.name = raw_voice.at("name").get<std::string>();
                    voice.ref_audio_path = raw_voice.at("ref_audio_path").get<std::string>();
                    voice.ref_text = raw_voice.at("ref_text").get<std::string>();
                    voice.ref_language = raw_voice.at("ref_language").get<std::string>();
                    if (voice.id.empty() || voice.id.size() > 64 || voice.name.empty() ||
                    voice.name.size() > 127 ||
                        std::any_of(parsed.voices.begin(), parsed.voices.end(),
                            [&voice](const TtsVoicePreset &existing) {
                                return existing.id == voice.id;
                            }))
                        throw std::runtime_error("voice IDs or names are invalid or duplicated");
                    parsed.voices.push_back(std::move(voice));
                }
                parsed.selected_voice_id =
                    json.at("selected_voice_id").get<std::string>();
                if (!SelectedVoice(parsed))
                    throw std::runtime_error("selected_voice_id does not name a voice");
                parsed.text_language = json.at("text_language").get<std::string>();
                parsed.streaming = mode == "stream";
                parsed.output_directory = json.at("output_directory").get<std::string>();
                if (!ValidateSettings(parsed, diagnostic))
                    return false;
                settings = std::move(parsed);
                diagnostic.clear();
                return true;
            }
            catch (const std::exception &error)
            {
                diagnostic = std::string("Invalid TTS settings: ") + error.what();
                return false;
            }
        }

        bool ReplaceFile(const std::filesystem::path &source,
                         const std::filesystem::path &destination,
                         std::string &diagnostic)
        {
#ifdef _WIN32
            if (MoveFileExW(source.c_str(), destination.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
            {
                diagnostic = "Could not publish TTS settings";
                return false;
            }
#else
            std::error_code error;
            std::filesystem::rename(source, destination, error);
            if (error)
            {
                diagnostic = "Could not publish TTS settings: " + error.message();
                return false;
            }
#endif
            return true;
        }
    }

    std::filesystem::path TtsSettingsPath()
    {
        return project_root / "config" / "tts" / "settings.json";
    }

    bool SaveSettings(const std::filesystem::path &path,
                      const TtsEditorSettings &settings, std::string &diagnostic)
    {
        if (!ValidateSettings(settings, diagnostic))
            return false;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            diagnostic = "Could not create the TTS settings directory: " + error.message();
            return false;
        }
        const auto temporary = std::filesystem::path(path.native() +
            std::filesystem::path::string_type(
#ifdef _WIN32
                L".tmp"
#else
                ".tmp"
#endif
            ));
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                diagnostic = "Could not write temporary TTS settings";
                return false;
            }
            output << Serialize(settings).dump(2) << '\n';
            output.flush();
            if (!output)
            {
                diagnostic = "Could not flush temporary TTS settings";
                return false;
            }
        }
        if (!ReplaceFile(temporary, path, diagnostic))
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    bool LoadOrCreateSettings(const std::filesystem::path &path,
                              TtsEditorSettings &settings, std::string &diagnostic)
    {
        std::error_code error;
        bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            diagnostic = "Could not inspect TTS settings: " + error.message();
            return false;
        }
        const auto settings_directory = path.parent_path();
        if (!exists && settings_directory.filename() == "tts" &&
            settings_directory.parent_path().filename() == "config")
        {
            const auto project = settings_directory.parent_path().parent_path();
            const auto legacy_path = project / "tts" / "settings.json";
            const bool legacy_exists = std::filesystem::exists(legacy_path, error);
            if (error)
            {
                diagnostic = "Could not inspect legacy TTS settings: " + error.message();
                return false;
            }
            if (legacy_exists)
            {
                std::filesystem::create_directories(settings_directory, error);
                if (!error)
                    std::filesystem::copy_file(legacy_path, path,
                        std::filesystem::copy_options::none, error);
                if (error)
                {
                    diagnostic = "Could not migrate legacy TTS settings: " + error.message();
                    return false;
                }
                exists = true;
            }
        }
        if (!exists && !SaveSettings(path, TtsEditorSettings{}, diagnostic))
            return false;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > kMaximumSettingsBytes)
        {
            diagnostic = "TTS settings are unreadable or exceed 256 KiB";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            diagnostic = "Could not open TTS settings";
            return false;
        }
        try
        {
            return Parse(Json::parse(input), settings, diagnostic);
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Invalid TTS settings JSON: ") + error.what();
            return false;
        }
    }

    bool ValidateForGeneration(const TtsEditorSettings &settings,
                               std::string &diagnostic)
    {
        if (!ValidateSettings(settings, diagnostic))
            return false;
        const TtsVoicePreset *voice = SelectedVoice(settings);
        if (settings.address.empty()) diagnostic = "Set the TTS server address";
        else if (settings.port == 0) diagnostic = "Set the TTS server port";
        else if (settings.api_path.empty() || settings.api_path.front() != '/')
            diagnostic = "Set a valid TTS API path";
        else if (!voice) diagnostic = "Select a voice preset";
        else if (voice->ref_audio_path.empty()) diagnostic = "Set server-side reference audio";
        else if (voice->ref_text.empty()) diagnostic = "Set reference text";
        else if (voice->ref_language.empty()) diagnostic = "Set reference language";
        else if (settings.text_language.empty()) diagnostic = "Set text language";
        else { diagnostic.clear(); return true; }
        return false;
    }

    const TtsVoicePreset *SelectedVoice(const TtsEditorSettings &settings)
    {
        const auto found = std::find_if(settings.voices.begin(), settings.voices.end(),
            [&settings](const TtsVoicePreset &voice) {
                return voice.id == settings.selected_voice_id;
            });
        return found == settings.voices.end() ? nullptr : &*found;
    }

    bool ValidateSettings(const TtsEditorSettings &settings,
                          std::string &diagnostic)
    {
        if (settings.voices.empty() || settings.voices.size() > 16)
            diagnostic = "Keep 1 to 16 voice presets";
        else if (settings.timeout_seconds == 0 || settings.timeout_seconds > 3600)
            diagnostic = "Set a timeout from 1 to 3600 seconds";
        else if (settings.address.size() > 255 || settings.api_path.size() > 127)
            diagnostic = "Server address or API path is too long";
        else if (settings.text_language.size() > 63 ||
                 settings.output_directory.size() > 511)
            diagnostic = "Language or output directory is too long";
        else if (!SelectedVoice(settings))
            diagnostic = "Select an existing voice preset";
        else
        {
            for (std::size_t index = 0; index < settings.voices.size(); ++index)
            {
                const auto &voice = settings.voices[index];
                if (voice.id.empty() || voice.id.size() > 64 ||
                    voice.name.empty() || voice.name.size() > 127 ||
                    voice.ref_audio_path.size() > 1023 ||
                    voice.ref_text.size() > 2047 ||
                    voice.ref_language.size() > 63 ||
                    std::any_of(settings.voices.begin(),
                                settings.voices.begin() + index,
                        [&voice](const TtsVoicePreset &previous) {
                            return previous.id == voice.id;
                        }))
                {
                    diagnostic = "Voice names, IDs, or reference fields are invalid";
                    return false;
                }
            }
            diagnostic.clear();
            return true;
        }
        return false;
    }
}
