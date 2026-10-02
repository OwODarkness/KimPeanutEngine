#ifndef KPENGINE_RUNTIME_ASSET_AUDIO_IMPORT_SERVICE_H
#define KPENGINE_RUNTIME_ASSET_AUDIO_IMPORT_SERVICE_H

#include <cstdint>
#include <filesystem>
#include <string>

#include "asset_import_registry.h"
#include "model_archive.h"

namespace kpengine::asset
{
    struct AudioImportSummary
    {
        std::string normalized_source_path;
        ContentHash product_hash{};
        std::filesystem::path product_path;
        std::string content_id;
        std::filesystem::path content_metadata_path;
        std::uint64_t duration_frames{};
        std::uint32_t waveform_buckets{};
        std::uint32_t subtitle_cues{};
        bool up_to_date{false};
    };

    struct AudioArchiveStatus
    {
        std::string normalized_source_path;
        ArchiveProbeStatus status{ArchiveProbeStatus::SourceNotFound};
        std::string diagnostic;
        ContentHash product_hash{};
        std::filesystem::path product_path;
    };

    class AudioImportService final
    {
    public:
        explicit AudioImportService(std::int32_t archive_busy_timeout_ms = 2500);

        AudioImportSummary Import(const ImportProviderRequest &request) const;
        AudioArchiveStatus Status(const ImportProviderRequest &request) const;

    private:
        std::int32_t archive_busy_timeout_ms_{};
    };

    const char *ArchiveProbeStatusName(ArchiveProbeStatus status) noexcept;
}

#endif
