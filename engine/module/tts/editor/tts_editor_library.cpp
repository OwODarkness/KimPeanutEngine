#include "tts_editor_library.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "runtime/core/config/path.h"
#include "tts_editor_wav.h"

namespace kpengine::tts_editor
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::uintmax_t kMaximumManifestBytes = 4u * 1024u * 1024u;
        constexpr std::size_t kMaximumDialogs = 10000;

        bool ValidArtifact(const std::string &name)
        {
            if (name.size() < 5 || !name.ends_with(".wav")) return false;
            return std::all_of(name.begin(), name.end() - 4,
                [](char ch) { return (ch >= '0' && ch <= '9') || ch == '-'; });
        }

        bool ValidBasename(const std::string &name)
        {
            if (name.empty() || name.size() > 120 || name.back() == '.' ||
                name.back() == ' ') return false;
            if (std::any_of(name.begin(), name.end(), [](unsigned char ch) {
                return ch < 32 || ch == '/' || ch == '\\' || ch == ':' ||
                    ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
                    ch == '>' || ch == '|';
            })) return false;
            std::string stem = name.substr(0, name.find('.'));
            std::transform(stem.begin(), stem.end(), stem.begin(),
                [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            static const std::set<std::string> reserved{
                "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
                "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
            return !reserved.contains(stem);
        }

        bool ReadBounded(const std::filesystem::path &path, std::uintmax_t limit,
                         std::vector<std::uint8_t> &bytes, std::string &diagnostic)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > limit)
            {
                diagnostic = "Audio or library file is missing, empty, or too large";
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || !stream.read(reinterpret_cast<char *>(bytes.data()),
                                        static_cast<std::streamsize>(bytes.size())))
            {
                diagnostic = "Could not read audio or library file";
                bytes.clear();
                return false;
            }
            return true;
        }

        bool Publish(std::span<const std::uint8_t> bytes,
                     const std::filesystem::path &destination,
                     bool replace, std::string &diagnostic)
        {
            std::error_code error;
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
            {
                diagnostic = "Could not create output directory: " + error.message();
                return false;
            }
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto temporary = destination.parent_path() /
                (destination.filename().wstring() + L"." + std::to_wstring(nonce) + L".tmp");
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                if (!stream || !stream.write(reinterpret_cast<const char *>(bytes.data()),
                                             static_cast<std::streamsize>(bytes.size())))
                {
                    diagnostic = "Could not write temporary output";
                    std::filesystem::remove(temporary, error);
                    return false;
                }
                stream.flush();
                if (!stream)
                {
                    diagnostic = "Could not flush temporary output";
                    stream.close();
                    std::filesystem::remove(temporary, error);
                    return false;
                }
            }
#ifdef _WIN32
            const DWORD flags = MOVEFILE_WRITE_THROUGH |
                (replace ? MOVEFILE_REPLACE_EXISTING : 0);
            const bool published = MoveFileExW(temporary.c_str(), destination.c_str(), flags) != 0;
#else
            const bool published = replace
                ? (std::filesystem::rename(temporary, destination, error), !error)
                : (std::filesystem::create_hard_link(temporary, destination, error), !error);
            if (published && !replace) std::filesystem::remove(temporary, error);
#endif
            if (!published)
            {
                std::filesystem::remove(temporary, error);
                diagnostic = "Could not atomically publish output";
                return false;
            }
            diagnostic.clear();
            return true;
        }

        bool ValidateDialogs(const std::vector<StoredDialog> &dialogs,
                             std::uint64_t next_id, std::string &diagnostic)
        {
            if (dialogs.size() > kMaximumDialogs || next_id == 0)
            {
                diagnostic = "TTS library capacity or next ID is invalid";
                return false;
            }
            std::set<std::uint64_t> ids;
            for (const auto &dialog : dialogs)
            {
        if (dialog.id == 0 || dialog.id >= next_id ||
                    !ids.insert(dialog.id).second || dialog.text.size() > 4096 ||
                    dialog.voice_name.size() > 127 || !ValidArtifact(dialog.artifact))
                {
                    diagnostic = "TTS library contains an invalid dialog";
                    return false;
                }
            }
            return true;
        }
    }

    TtsEditorLibrary::TtsEditorLibrary(std::filesystem::path root)
        : root_(std::move(root)) {}

    std::filesystem::path TtsEditorLibrary::DefaultRoot()
    {
        return project_root / "save" / "tts_editor";
    }

    bool TtsEditorLibrary::Load(std::vector<StoredDialog> &dialogs,
                                std::uint64_t &next_id,
                                std::string &diagnostic) const
    {
        const auto manifest = root_ / "library.json";
        std::error_code error;
        const auto manifest_status = std::filesystem::symlink_status(manifest, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            dialogs.clear();
            next_id = 1;
            diagnostic.clear();
            return true;
        }
        if (error)
        {
            diagnostic = "Could not inspect TTS library";
            return false;
        }
        if (!std::filesystem::exists(manifest_status))
        {
            dialogs.clear();
            next_id = 1;
            diagnostic.clear();
            return true;
        }
        if (std::filesystem::is_symlink(manifest_status) ||
            !std::filesystem::is_regular_file(manifest_status))
        {
            diagnostic = "TTS library manifest path is unsafe";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!ReadBounded(manifest, kMaximumManifestBytes, bytes, diagnostic))
            return false;
        try
        {
            const Json json = Json::parse(bytes.begin(), bytes.end());
            if (!json.is_object() || json.at("schema_version").get<int>() != 1 ||
                !json.at("dialogs").is_array())
                throw std::runtime_error("unsupported library schema");
            std::vector<StoredDialog> parsed;
            for (const auto &row : json.at("dialogs"))
                parsed.push_back({row.at("id").get<std::uint64_t>(),
                                  row.at("text").get<std::string>(),
                                  row.at("voice_name").get<std::string>(),
                                  row.at("generated_streaming").get<bool>(),
                                  row.at("artifact").get<std::string>()});
            const auto parsed_next = json.at("next_id").get<std::uint64_t>();
            if (!ValidateDialogs(parsed, parsed_next, diagnostic)) return false;
            for (const auto &row : parsed)
            {
                const auto audio_directory = root_ / "audio";
                const auto artifact_path = audio_directory / row.artifact;
                const auto directory_status = std::filesystem::symlink_status(audio_directory, error);
                const auto artifact_status = std::filesystem::symlink_status(artifact_path, error);
                if (error || std::filesystem::is_symlink(directory_status) ||
                    std::filesystem::is_symlink(artifact_status) ||
                    !std::filesystem::is_regular_file(artifact_status))
                {
                    diagnostic = "TTS library references a missing WAV artifact";
                    return false;
                }
            }
            dialogs = std::move(parsed);
            next_id = parsed_next;
            diagnostic.clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            diagnostic = std::string("Invalid TTS library: ") + exception.what();
            return false;
        }
    }

    bool TtsEditorLibrary::Save(const std::vector<StoredDialog> &dialogs,
                                std::uint64_t next_id,
                                std::string &diagnostic) const
    {
        if (!ValidateDialogs(dialogs, next_id, diagnostic)) return false;
        Json rows = Json::array();
        for (const auto &row : dialogs)
            rows.push_back({{"id", row.id}, {"text", row.text},
                            {"voice_name", row.voice_name},
                            {"generated_streaming", row.generated_streaming},
                            {"artifact", row.artifact}});
        const std::string serialized = Json{{"schema_version", 1},
            {"next_id", next_id}, {"dialogs", std::move(rows)}}.dump(2) + '\n';
        if (serialized.size() > kMaximumManifestBytes)
        {
            diagnostic = "TTS library manifest exceeds 4 MiB";
            return false;
        }
        return Publish({reinterpret_cast<const std::uint8_t *>(serialized.data()),
                        serialized.size()}, root_ / "library.json", true, diagnostic);
    }

    bool TtsEditorLibrary::Store(std::uint64_t id,
                                 std::span<const std::uint8_t> wav,
                                 std::string &artifact,
                                 std::string &diagnostic) const
    {
        if (id == 0)
        {
            diagnostic = "Invalid dialog ID";
            return false;
        }
        std::vector<std::uint8_t> canonical;
        if (!CanonicalizeWav(wav, canonical, diagnostic)) return false;
        for (std::uint32_t attempt = 0; attempt < 16; ++attempt)
        {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count() + attempt;
            const std::string name = std::to_string(id) + "-" + std::to_string(nonce) + ".wav";
            if (Publish(canonical, root_ / "audio" / name, false, diagnostic))
            {
                artifact = name;
                return true;
            }
            std::error_code error;
            if (!std::filesystem::exists(root_ / "audio" / name, error)) return false;
        }
        diagnostic = "Could not allocate a unique WAV artifact name";
        return false;
    }

    bool TtsEditorLibrary::Read(const std::string &artifact,
                                std::vector<std::uint8_t> &wav,
                                std::string &diagnostic) const
    {
        if (!ValidArtifact(artifact))
        {
            diagnostic = "Invalid WAV artifact name";
            return false;
        }
        std::error_code path_error;
        const auto audio_directory = root_ / "audio";
        const auto artifact_path = audio_directory / artifact;
        const auto directory_status = std::filesystem::symlink_status(audio_directory, path_error);
        const auto artifact_status = std::filesystem::symlink_status(artifact_path, path_error);
        if (path_error || std::filesystem::is_symlink(directory_status) ||
            std::filesystem::is_symlink(artifact_status) ||
            !std::filesystem::is_regular_file(artifact_status))
        {
            diagnostic = "WAV artifact path is missing or unsafe";
            return false;
        }
        if (!ReadBounded(artifact_path, kMaximumWavBytes, wav, diagnostic))
            return false;
        std::vector<std::uint8_t> checked;
        if (!CanonicalizeWav(wav, checked, diagnostic)) return false;
        wav = std::move(checked);
        return true;
    }

    bool TtsEditorLibrary::Export(const std::string &artifact,
                                  const std::filesystem::path &directory,
                                  const std::string &basename,
                                  std::filesystem::path &published_path,
                                  std::string &diagnostic) const
    {
        if (!ValidBasename(basename))
        {
            diagnostic = "Export filename must be a safe basename";
            return false;
        }
        std::vector<std::uint8_t> wav;
        if (!Read(artifact, wav, diagnostic)) return false;
        const auto target_directory = directory.is_absolute() ? directory : project_root / directory;
        for (std::uint32_t suffix = 1; suffix <= 10000; ++suffix)
        {
            const auto filename = basename + (suffix == 1 ? "" : "-" + std::to_string(suffix)) + ".wav";
            const auto target = target_directory / filename;
            std::error_code error;
            if (std::filesystem::exists(target, error) || error) continue;
            if (Publish(wav, target, false, diagnostic))
            {
                published_path = target;
                return true;
            }
            if (!std::filesystem::exists(target, error)) return false;
        }
        diagnostic = "No free WAV filename is available";
        return false;
    }
}
