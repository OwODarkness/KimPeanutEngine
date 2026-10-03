#ifndef KPENGINE_MODULE_TTS_EDITOR_LIBRARY_H
#define KPENGINE_MODULE_TTS_EDITOR_LIBRARY_H

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace kpengine::tts_editor
{
    struct StoredDialog
    {
        std::uint64_t id = 0;
        std::string text;
        std::string voice_name;
        bool generated_streaming = false;
        std::string artifact;
    };

    class TtsEditorLibrary final
    {
    public:
        explicit TtsEditorLibrary(std::filesystem::path root);
        static std::filesystem::path DefaultRoot();

        bool Load(std::vector<StoredDialog> &dialogs, std::uint64_t &next_id,
                  std::string &diagnostic) const;
        bool Save(const std::vector<StoredDialog> &dialogs, std::uint64_t next_id,
                  std::string &diagnostic) const;
        bool Store(std::uint64_t id, std::span<const std::uint8_t> wav,
                   std::string &artifact, std::string &diagnostic) const;
        bool Read(const std::string &artifact, std::vector<std::uint8_t> &wav,
                  std::string &diagnostic) const;
        bool Export(const std::string &artifact,
                    const std::filesystem::path &directory,
                    const std::string &basename,
                    std::filesystem::path &published_path,
                    std::string &diagnostic) const;

    private:
        std::filesystem::path root_;
    };
}

#endif
