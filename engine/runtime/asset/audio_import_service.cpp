#include "audio_import_service.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#include "audio_importer.h"
#include "native_audio.h"

namespace kpengine::asset
{
    namespace
    {
        using AudioMetadata = SourceRecord::AudioMetadata;

        struct ResolvedInput
        {
            std::string normalized_path;
            std::filesystem::path absolute_path;
        };

        struct HashedInput
        {
            ResolvedInput input;
            SourceDependencyRecord dependency;
        };

        [[noreturn]] void Fail(std::string message)
        {
            throw AudioImportError(std::move(message));
        }

        std::filesystem::path PathFromUtf8(std::string_view value)
        {
            return std::filesystem::path{std::u8string{
                reinterpret_cast<const char8_t *>(value.data()), value.size()}};
        }

        std::string PathToUtf8(const std::filesystem::path &path)
        {
            const std::u8string value = path.generic_u8string();
            return {reinterpret_cast<const char *>(value.data()), value.size()};
        }

        ResolvedInput ResolveInput(const std::filesystem::path &asset_root,
                                   const std::filesystem::path &relative_path,
                                   std::string_view label)
        {
            if (asset_root.empty() || relative_path.empty() || relative_path.is_absolute())
                Fail(std::string{label} + " path must be relative to the Asset root");
            std::string normalized;
            try
            {
                normalized = NormalizeAssetRelativePath(PathToUtf8(relative_path));
            }
            catch (const std::exception &error)
            {
                Fail(std::string{label} + " path is invalid: " + error.what());
            }

            std::error_code error;
            const auto root = std::filesystem::weakly_canonical(asset_root, error);
            if (error || !std::filesystem::is_directory(root))
                Fail("Asset root does not exist or cannot be resolved");
            const auto absolute = std::filesystem::weakly_canonical(
                root / PathFromUtf8(normalized), error);
            if (error || !std::filesystem::is_regular_file(absolute))
                Fail(std::string{label} + " file does not exist or cannot be resolved");
            const auto relative = absolute.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() ||
                std::any_of(relative.begin(), relative.end(), [](const auto &part)
                {
                    return part == "..";
                }))
            {
                Fail(std::string{label} + " path resolves outside the Asset root");
            }
            return {std::move(normalized), absolute};
        }

        HashedInput HashInput(const std::filesystem::path &asset_root,
                              const std::filesystem::path &relative_path,
                              std::string_view label, std::uint64_t max_bytes)
        {
            ResolvedInput resolved = ResolveInput(asset_root, relative_path, label);
            std::error_code error;
            const std::uint64_t byte_size = std::filesystem::file_size(resolved.absolute_path, error);
            if (error || byte_size == 0 || byte_size > max_bytes)
                Fail(std::string{label} + " is empty, unreadable, or exceeds its size limit");
            const auto write_time = std::filesystem::last_write_time(resolved.absolute_path, error);
            if (error) Fail(std::string{label} + " modification time is unavailable");
            const ContentHash hash = Sha256File(resolved.absolute_path);
            const auto size_after = std::filesystem::file_size(resolved.absolute_path, error);
            if (error || size_after != byte_size)
                Fail(std::string{label} + " changed while it was being hashed");
            const auto time_after = std::filesystem::last_write_time(resolved.absolute_path, error);
            if (error || time_after != write_time)
                Fail(std::string{label} + " changed while it was being hashed");
            SourceDependencyRecord dependency{resolved.normalized_path, hash, byte_size,
                     static_cast<std::int64_t>(write_time.time_since_epoch().count())};
            return {std::move(resolved), std::move(dependency)};
        }

        ContentHash SettingsHash(const AudioMetadata &metadata)
        {
            std::vector<std::byte> bytes;
            constexpr std::string_view domain = "KPENGINE_AUDIO_IMPORT_SETTINGS_V1";
            bytes.insert(bytes.end(), reinterpret_cast<const std::byte *>(domain.data()),
                         reinterpret_cast<const std::byte *>(domain.data() + domain.size()));
            const auto append_string = [&bytes](std::string_view value)
            {
                std::uint64_t size = value.size();
                for (unsigned shift = 0; shift < 64; shift += 8)
                    bytes.push_back(static_cast<std::byte>((size >> shift) & 0xffu));
                bytes.insert(bytes.end(), reinterpret_cast<const std::byte *>(value.data()),
                             reinterpret_cast<const std::byte *>(value.data() + value.size()));
            };
            append_string(metadata.subtitle_path);
            append_string(metadata.subtitle_language);
            return Sha256(bytes);
        }

        std::vector<SourceFingerprintInput> Fingerprints(
            const std::vector<SourceDependencyRecord> &dependencies)
        {
            std::vector<SourceFingerprintInput> inputs;
            inputs.reserve(dependencies.size());
            for (const SourceDependencyRecord &dependency : dependencies)
                inputs.push_back({dependency.normalized_path, dependency.content_hash});
            return inputs;
        }

        std::vector<HashedInput> HashInputs(const std::filesystem::path &asset_root,
                                            const std::filesystem::path &source_path,
                                            const AudioMetadata &metadata)
        {
            std::vector<HashedInput> inputs;
            inputs.push_back(HashInput(asset_root, source_path, "audio source",
                                       kNativeAudioMaxSourceBytes));
            if (!metadata.subtitle_path.empty())
                inputs.push_back(HashInput(asset_root, PathFromUtf8(metadata.subtitle_path),
                                           "subtitle", kNativeAudioMaxSubtitleBytes));
            std::sort(inputs.begin(), inputs.end(), [](const HashedInput &left,
                                                       const HashedInput &right)
            {
                return left.input.normalized_path < right.input.normalized_path;
            });
            if (inputs.size() == 2 &&
                inputs[0].input.normalized_path == inputs[1].input.normalized_path)
                Fail("audio and subtitle inputs resolve to the same Asset path");
            return inputs;
        }

        std::vector<SourceDependencyRecord> DependencyRecords(
            const std::vector<HashedInput> &inputs)
        {
            std::vector<SourceDependencyRecord> dependencies;
            dependencies.reserve(inputs.size());
            for (const HashedInput &input : inputs) dependencies.push_back(input.dependency);
            return dependencies;
        }

        std::filesystem::path AbsoluteRoot(const std::filesystem::path &path,
                                           std::string_view label)
        {
            if (path.empty()) Fail(std::string{label} + " is required");
            std::error_code error;
            auto absolute = std::filesystem::absolute(path, error).lexically_normal();
            if (error) Fail(std::string{label} + " cannot be resolved");
            return absolute;
        }

        std::string SourceRelativePath(const std::filesystem::path &asset_root,
                                       const std::filesystem::path &source_path)
        {
            return ResolveInput(asset_root, source_path, "audio source").normalized_path;
        }

        std::optional<AudioMetadata> ResolveOptions(
            const ImportProviderRequest &request,
            const std::optional<SourceArchiveSnapshot> &existing)
        {
            if (request.clear_subtitle)
            {
                if (!request.reimport || request.audio_options.has_value())
                    Fail("--clear-subtitle is valid only on reimport and conflicts with subtitle options");
                return AudioMetadata{kNativeAudioVersion, {}, "und"};
            }

            if (request.audio_options.has_value())
            {
                const AudioImportOptions &options = *request.audio_options;
                if (!options.subtitle_path.has_value() || options.subtitle_path->empty())
                    Fail("subtitle language options require an explicit subtitle path");
                const ResolvedInput subtitle = ResolveInput(request.asset_root,
                    *options.subtitle_path, "subtitle");
                return AudioMetadata{kNativeAudioVersion, subtitle.normalized_path,
                                     options.subtitle_language.empty()
                                         ? "und" : options.subtitle_language};
            }

            if (request.reimport && existing.has_value() &&
                existing->source.audio_metadata.has_value())
            {
                AudioMetadata metadata = *existing->source.audio_metadata;
                metadata.native_audio_version = kNativeAudioVersion;
                return metadata;
            }
            return AudioMetadata{kNativeAudioVersion, {}, "und"};
        }

        const ProductRecord *AudioProduct(const SourceArchiveSnapshot &snapshot,
                                          ContentHash &hash)
        {
            for (const SourceProductRecord &link : snapshot.source_products)
            {
                if (link.asset_type != ArchiveProductType::Audio) continue;
                hash = link.content_hash;
                const auto found = std::find_if(snapshot.products.begin(), snapshot.products.end(),
                    [&link](const ProductRecord &candidate)
                    {
                        return candidate.asset_type == ArchiveProductType::Audio &&
                               candidate.content_hash == link.content_hash;
                    });
                return found == snapshot.products.end() ? nullptr : &*found;
            }
            return nullptr;
        }

        ArchiveProbeStatus MissingStatus(std::string_view message) noexcept
        {
            return message.starts_with("subtitle") ? ArchiveProbeStatus::DependencyMissing
                                                    : ArchiveProbeStatus::SourceInputMissing;
        }
    }

    AudioImportService::AudioImportService(std::int32_t archive_busy_timeout_ms)
        : archive_busy_timeout_ms_(archive_busy_timeout_ms)
    {
        if (archive_busy_timeout_ms_ < 0)
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "archive busy timeout cannot be negative");
    }

    AudioImportSummary AudioImportService::Import(const ImportProviderRequest &request) const
    {
        if (request.clear_subtitle && !request.reimport)
            Fail("--clear-subtitle is valid only on reimport");
        const std::filesystem::path asset_root = AbsoluteRoot(request.asset_root, "Asset root");
        const std::filesystem::path archive_root = AbsoluteRoot(request.archive_root, "archive root");
        const std::string source_path = SourceRelativePath(asset_root, request.source_path);
        std::error_code error;
        std::filesystem::create_directories(archive_root, error);
        if (error) Fail("failed to create archive root: " + error.message());

        ModelArchiveDatabase archive{archive_root / "archive.sqlite3", archive_busy_timeout_ms_};
        const auto existing = archive.FindSource(source_path);
        const auto metadata = ResolveOptions(request, existing);
        if (!metadata.has_value()) Fail("Audio import options are unavailable");
        const auto inputs = HashInputs(asset_root, request.source_path, *metadata);
        const auto dependencies = DependencyRecords(inputs);
        const ContentHash package_hash = HashSourcePackage(Fingerprints(dependencies));
        const ContentHash settings_hash = SettingsHash(*metadata);
        const SourceProbeRequest probe_request{
            source_path, package_hash, "audio", kAudioImporterVersion,
            settings_hash, 0, metadata};

        const ArchiveProbeResult probe = archive.ProbeSource(probe_request);
        if (probe.status == ArchiveProbeStatus::UpToDate && probe.snapshot.has_value())
        {
            ContentHash hash{};
            const ProductRecord *product = AudioProduct(*probe.snapshot, hash);
            if (product == nullptr)
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                        "up-to-date Audio source has no Audio product");
            return {source_path, hash, archive_root / product->relative_path,
                    0, 0, 0, true};
        }

        AudioImportRequest importer_request{};
        importer_request.asset_root = asset_root;
        importer_request.source_path = request.source_path;
        if (!metadata->subtitle_path.empty())
        {
            importer_request.options.subtitle_path = PathFromUtf8(metadata->subtitle_path);
            importer_request.options.subtitle_language = metadata->subtitle_language;
        }
        CookedAudio cooked = AudioImporter{}.Import(importer_request);

        const auto verified_inputs = HashInputs(asset_root, request.source_path, *metadata);
        const auto verified_dependencies = DependencyRecords(verified_inputs);
        if (HashSourcePackage(Fingerprints(verified_dependencies)) != package_hash)
            Fail("audio or subtitle source changed during import; retry the operation");

        const std::filesystem::path source_file = PathFromUtf8(source_path);
        const std::u8string filename_u8 = source_file.stem().generic_u8string();
        const std::string filename{reinterpret_cast<const char *>(filename_u8.data()),
                                   filename_u8.size()};
        SourceRecord source{};
        source.normalized_path = source_path;
        source.path_hash = Sha256(source_path);
        source.display_name = filename.empty() ? source_path : filename;
        source.package_hash = package_hash;
        source.importer_id = "audio";
        source.importer_version = kAudioImporterVersion;
        source.settings_hash = settings_hash;
        source.native_model_version = 0;
        source.status = SourceImportStatus::Ready;
        source.audio_metadata = metadata;

        const std::string relative_product =
            ProductRelativePath(ArchiveProductType::Audio, cooked.product_hash);
        ProductRecord product{cooked.product_hash, ArchiveProductType::Audio,
                              relative_product, cooked.bytes.size(), kNativeAudioVersion};
        const SourceProductRecord source_product{
            cooked.product_hash, ArchiveProductType::Audio, 1, -1,
            filename};

        PublishCookedAudioProduct(archive_root, cooked);
        archive.ReplaceSource(source, verified_dependencies, {product}, {source_product}, {});
        return {source_path, cooked.product_hash, archive_root / relative_product,
                cooked.data.duration_frames,
                static_cast<std::uint32_t>(cooked.data.waveform.size()),
                static_cast<std::uint32_t>(cooked.data.subtitles.size()), false};
    }

    AudioArchiveStatus AudioImportService::Status(const ImportProviderRequest &request) const
    {
        const std::filesystem::path asset_root = AbsoluteRoot(request.asset_root, "Asset root");
        const std::filesystem::path archive_root = AbsoluteRoot(request.archive_root, "archive root");
        const std::string source_path = NormalizeAssetRelativePath(PathToUtf8(request.source_path));
        const auto database_path = archive_root / "archive.sqlite3";
        if (!std::filesystem::is_regular_file(database_path))
            return {source_path, ArchiveProbeStatus::SourceNotFound,
                    "source is not present in the archive", {}, {}};
        ModelArchiveDatabase archive{database_path, archive_busy_timeout_ms_,
                                     ModelArchiveOpenMode::ReadOnly};
        const auto existing = archive.FindSource(source_path);
        if (!existing.has_value())
            return {source_path, ArchiveProbeStatus::SourceNotFound,
                    "source is not present in the archive", {}, {}};
        if (!existing->source.audio_metadata.has_value())
            return {source_path, ArchiveProbeStatus::SourceNotFound,
                    "source does not have an Audio product", {}, {}};

        try
        {
            const AudioMetadata &metadata = *existing->source.audio_metadata;
            const auto inputs = HashInputs(asset_root, request.source_path, metadata);
            const auto dependencies = DependencyRecords(inputs);
            const ContentHash package_hash = HashSourcePackage(Fingerprints(dependencies));
            const SourceProbeRequest probe_request{
                source_path, package_hash, "audio", kAudioImporterVersion,
                SettingsHash(metadata), 0, metadata};
            const ArchiveProbeResult probe = archive.ProbeSource(probe_request);
            AudioArchiveStatus result{source_path, probe.status, probe.diagnostic, {}, {}};
            if (probe.snapshot.has_value())
            {
                ContentHash hash{};
                const ProductRecord *product = AudioProduct(*probe.snapshot, hash);
                if (product != nullptr)
                {
                    result.product_hash = hash;
                    result.product_path = archive_root / product->relative_path;
                }
            }
            return result;
        }
        catch (const AudioImportError &error)
        {
            return {source_path, MissingStatus(error.what()), error.what(), {}, {}};
        }
    }

    const char *ArchiveProbeStatusName(ArchiveProbeStatus status) noexcept
    {
        switch (status)
        {
        case ArchiveProbeStatus::SourceNotFound: return "SourceNotFound";
        case ArchiveProbeStatus::SourceFailed: return "SourceFailed";
        case ArchiveProbeStatus::SourcePackageChanged: return "SourceChanged";
        case ArchiveProbeStatus::ImporterChanged: return "ImporterChanged";
        case ArchiveProbeStatus::SettingsChanged: return "SettingsChanged";
        case ArchiveProbeStatus::NativeSchemaChanged: return "NativeSchemaChanged";
        case ArchiveProbeStatus::MissingProduct: return "MissingProduct";
        case ArchiveProbeStatus::CorruptProduct: return "CorruptProduct";
        case ArchiveProbeStatus::UpToDate: return "UpToDate";
        case ArchiveProbeStatus::SourceInputMissing: return "SourceInputMissing";
        case ArchiveProbeStatus::DependencyMissing: return "DependencyMissing";
        }
        return "Unknown";
    }
}
