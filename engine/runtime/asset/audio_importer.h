#ifndef KPENGINE_RUNTIME_ASSET_AUDIO_IMPORTER_H
#define KPENGINE_RUNTIME_ASSET_AUDIO_IMPORTER_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "asset_import_registry.h"
#include "native_audio.h"

namespace kpengine::asset
{
    constexpr std::uint32_t kAudioImporterVersion = 1;

    struct AudioImportRequest
    {
        std::filesystem::path asset_root;
        std::filesystem::path source_path;
        AudioImportOptions options{};
    };

    struct CookedAudio
    {
        NativeAudioData data;
        std::vector<std::byte> bytes;
        ContentHash product_hash{};
    };

    class AudioImportError final : public std::runtime_error
    {
    public:
        explicit AudioImportError(std::string message);
    };

    class AudioImporter final
    {
    public:
        CookedAudio Import(const AudioImportRequest &request) const;
    };

    CookedAudio ImportAudio(const AudioImportRequest &request);
    void PublishCookedAudioProduct(const std::filesystem::path &archive_root,
                                   const CookedAudio &cooked);
}

#endif
