#include "model_import_service.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

#include "assimp_model_decoder.h"
#include "native_material.h"
#include "native_model.h"
#include "native_texture.h"

namespace kpengine::asset
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        class MetricTimer final
        {
        public:
            MetricTimer(ModelImportMetrics &metrics, ModelImportMetricStage stage)
                : metrics_(metrics), stage_(stage), started_(Clock::now())
            {
            }

            ~MetricTimer() noexcept
            {
                const auto elapsed = std::chrono::duration<double>(Clock::now() - started_);
                metrics_.stage_seconds[static_cast<std::size_t>(stage_)] += elapsed.count();
            }

            MetricTimer(const MetricTimer &) = delete;
            MetricTimer &operator=(const MetricTimer &) = delete;

        private:
            ModelImportMetrics &metrics_;
            ModelImportMetricStage stage_;
            Clock::time_point started_;
        };

        struct ProcessSnapshot final
        {
            double cpu_seconds{};
            std::uint64_t peak_working_set_bytes{};
        };

#if defined(_WIN32)
        double FileTimeSeconds(const FILETIME &value) noexcept
        {
            ULARGE_INTEGER ticks{};
            ticks.LowPart = value.dwLowDateTime;
            ticks.HighPart = value.dwHighDateTime;
            return static_cast<double>(ticks.QuadPart) / 10000000.0;
        }
#endif

        ProcessSnapshot ReadProcessSnapshot() noexcept
        {
            ProcessSnapshot result{};
#if defined(_WIN32)
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != 0)
            {
                result.cpu_seconds = FileTimeSeconds(kernel) + FileTimeSeconds(user);
            }

            PROCESS_MEMORY_COUNTERS counters{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0)
            {
                result.peak_working_set_bytes = counters.PeakWorkingSetSize;
            }
#endif
            return result;
        }

        void FinalizeMetrics(ModelImportMetrics &metrics, Clock::time_point started,
                             const ProcessSnapshot &process_started)
        {
            const auto elapsed = std::chrono::duration<double>(Clock::now() - started);
            metrics.total_seconds = elapsed.count();
            const ProcessSnapshot process_finished = ReadProcessSnapshot();
            metrics.process_cpu_seconds =
                std::max(0.0, process_finished.cpu_seconds - process_started.cpu_seconds);
            metrics.peak_working_set_bytes = process_finished.peak_working_set_bytes;
            const unsigned logical_cpus = std::max(1u, std::thread::hardware_concurrency());
            metrics.logical_processor_count = logical_cpus;
            if (metrics.total_seconds > 0.0)
            {
                metrics.cpu_utilization_percent =
                    metrics.process_cpu_seconds / metrics.total_seconds /
                    static_cast<double>(logical_cpus) * 100.0;
            }
            const double write_seconds =
                metrics.stage_seconds[static_cast<std::size_t>(ModelImportMetricStage::StagingWrite)] +
                metrics.stage_seconds[static_cast<std::size_t>(ModelImportMetricStage::Publication)];
            if (write_seconds > 0.0)
            {
                metrics.storage_write_megabytes_per_second =
                    static_cast<double>(metrics.bytes_written) / (1024.0 * 1024.0) /
                    write_seconds;
            }
        }

        void ReportProgress(const ModelImportRequest &request, ModelImportProgressStage stage,
                            std::string message, std::size_t completed = 0,
                            std::size_t total = 0)
        {
            if (request.progress_callback)
            {
                request.progress_callback({stage, std::move(message), completed, total});
            }
        }

        constexpr std::int32_t kModelProductRole = 0;
        constexpr std::int32_t kMaterialProductRole = 1;

        [[noreturn]] void Fail(ModelImportErrorCode code, const std::string &message)
        {
            throw ModelImportError(code, message);
        }

        std::filesystem::path AbsoluteNormalized(const std::filesystem::path &path)
        {
            if (path.empty())
            {
                Fail(ModelImportErrorCode::InvalidArgument, "import path is empty");
            }
            return std::filesystem::absolute(path).lexically_normal();
        }

        std::filesystem::path ResolveSourcePath(const ModelImportRequest &request,
                                                 const std::filesystem::path &asset_root)
        {
            const std::filesystem::path candidate = request.source_path.is_absolute()
                                                         ? request.source_path
                                                         : asset_root / request.source_path;
            const std::filesystem::path source_path = AbsoluteNormalized(candidate);
            std::error_code error;
            if (!std::filesystem::is_regular_file(source_path, error) || error)
            {
                Fail(ModelImportErrorCode::IoError,
                     "model source is missing or is not a regular file: " + source_path.string());
            }
            return source_path;
        }

        std::string AssetRelativePath(const std::filesystem::path &asset_root,
                                      const std::filesystem::path &path)
        {
            const std::filesystem::path relative =
                AbsoluteNormalized(path).lexically_relative(asset_root);
            const std::string relative_text = relative.generic_string();
            if (relative.empty() || relative.is_absolute() || relative_text == ".." ||
                relative_text.rfind("../", 0) == 0)
            {
                Fail(ModelImportErrorCode::InvalidArgument,
                     "path escapes the Asset root: " + path.string());
            }
            try
            {
                return NormalizeAssetRelativePath(relative_text);
            }
            catch (const ModelArchiveError &error)
            {
                Fail(ModelImportErrorCode::InvalidArgument, error.what());
            }
        }

        std::vector<std::byte> ReadBytes(const std::filesystem::path &path,
                                         std::uint64_t *bytes_read = nullptr)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                Fail(ModelImportErrorCode::IoError, "failed to open product: " + path.string());
            }
            const std::streampos end = file.tellg();
            if (end < 0)
            {
                Fail(ModelImportErrorCode::IoError, "failed to determine product size: " + path.string());
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(end));
            if (bytes_read != nullptr)
            {
                *bytes_read += bytes.size();
            }
            file.seekg(0, std::ios::beg);
            if (!bytes.empty())
            {
                file.read(reinterpret_cast<char *>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
                if (!file)
                {
                    Fail(ModelImportErrorCode::IoError, "failed to read product: " + path.string());
                }
            }
            return bytes;
        }

        void WriteBytes(const std::filesystem::path &path, const std::vector<std::byte> &bytes,
                        ModelImportMetrics *metrics = nullptr)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to create staging directory: " + error.message());
            }
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to create staged product: " + path.string());
            }
            if (!bytes.empty())
            {
                file.write(reinterpret_cast<const char *>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
            }
            if (!file.good())
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to write staged product: " + path.string());
            }
            if (metrics != nullptr)
            {
                ++metrics->product_write_count;
                metrics->bytes_written += bytes.size();
            }
        }

        std::vector<std::byte> SettingsBytes(const ModelImportSettings &settings)
        {
            std::vector<std::byte> bytes;
            const auto append_u32 = [&bytes](std::uint32_t value)
            {
                for (unsigned shift = 0; shift < 32; shift += 8)
                {
                    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
                }
            };
            const auto append_string = [&bytes, &append_u32](std::string_view value)
            {
                append_u32(static_cast<std::uint32_t>(value.size()));
                for (const char character : value)
                {
                    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
                }
            };
            append_string("KPENGINE_IMPORT_SETTINGS_V3");
            append_string(settings.importer_id);
            append_u32(settings.importer_version);
            append_u32(settings.native_model_version);
            append_u32(settings.material_schema_version);
            append_string(settings.shader_asset_path);
            append_u32(settings.texture_settings.max_dimension);
            append_u32(settings.texture_settings.max_levels);
            append_u32(static_cast<std::uint32_t>(settings.texture_settings.compression));
            append_u32(settings.emit_texture_profile_variants ? 1U : 0U);
            return bytes;
        }

        ContentHash SettingsHash(const ModelImportSettings &settings)
        {
            if (settings.importer_id.empty() || settings.importer_version == 0 ||
                settings.native_model_version != kNativeModelVersion ||
                settings.material_schema_version != kNativeMaterialSchemaVersion ||
                settings.shader_asset_path.empty() ||
                settings.texture_settings.max_dimension == 0 ||
                settings.texture_settings.max_dimension > kNativeTextureMaxDimension ||
                settings.texture_settings.max_levels > kNativeTextureMaxMipLevels ||
                static_cast<std::uint8_t>(settings.texture_settings.compression) >
                    static_cast<std::uint8_t>(TextureCompressionPolicy::RequireBlockCompression))
            {
                Fail(ModelImportErrorCode::InvalidArgument, "model import settings are incomplete");
            }
            return Sha256(SettingsBytes(settings));
        }

        std::vector<SourceDependencyRecord> HashDependencies(
            const ImportedModelDocument &document, const std::filesystem::path &asset_root,
            const std::filesystem::path &source_path, const std::string &source_relative_path,
            ModelImportMetrics &metrics)
        {
            std::map<std::string, ContentHash> unique;
            for (const ImportedSourceDependency &dependency : document.source_dependencies)
            {
                // Embedded bytes are already covered by their containing GLB,
                // data URI, or other primary source. They have no filesystem
                // path that can be rehashed before decoding.
                if (dependency.kind == ImportedDependencyKind::EmbeddedData)
                {
                    continue;
                }
                const std::filesystem::path resolved =
                    dependency.kind == ImportedDependencyKind::PrimarySource
                        ? source_path
                        : dependency.resolved_path;
                if (resolved.empty())
                {
                    Fail(ModelImportErrorCode::IoError,
                         "import dependency has no resolved filesystem path: " + dependency.path);
                }
                const std::string normalized =
                    dependency.kind == ImportedDependencyKind::PrimarySource
                        ? source_relative_path
                        : AssetRelativePath(asset_root, resolved);
                std::error_code size_error;
                const std::uintmax_t byte_count = std::filesystem::file_size(resolved, size_error);
                if (size_error)
                {
                    Fail(ModelImportErrorCode::IoError,
                         "failed to determine dependency size: " + resolved.string() + ": " +
                             size_error.message());
                }
                metrics.source_bytes_read += byte_count;
                const ContentHash content_hash = Sha256File(resolved);
                const auto [iterator, inserted] = unique.emplace(normalized, content_hash);
                if (!inserted && iterator->second != content_hash)
                {
                    Fail(ModelImportErrorCode::InvalidArgument,
                         "import dependency paths collide after normalization: " + normalized);
                }
            }
            if (unique.empty())
            {
                Fail(ModelImportErrorCode::InvalidArgument, "import source has no hashable dependencies");
            }
            std::vector<SourceDependencyRecord> result;
            result.reserve(unique.size());
            for (const auto &[path, hash] : unique)
            {
                result.push_back({path, hash});
            }
            return result;
        }

        std::vector<SourceDependencyRecord> HashRecordedDependencies(
            const SourceArchiveSnapshot &snapshot, const std::filesystem::path &asset_root,
            ModelImportMetrics &metrics)
        {
            std::vector<SourceDependencyRecord> result;
            result.reserve(snapshot.dependencies.size());
            for (const SourceDependencyRecord &dependency : snapshot.dependencies)
            {
                const std::string normalized = NormalizeAssetRelativePath(dependency.normalized_path);
                const std::filesystem::path path = asset_root / normalized;
                std::error_code size_error;
                const std::uintmax_t byte_count = std::filesystem::file_size(path, size_error);
                if (size_error)
                {
                    throw ModelArchiveError(ModelArchiveErrorCode::IoError,
                                            "failed to determine dependency size: " + path.string());
                }
                metrics.source_bytes_read += byte_count;
                const ContentHash current = Sha256File(path);
                result.push_back({normalized, current});
            }
            return result;
        }

        std::filesystem::path ProductPath(const std::filesystem::path &archive_root,
                                          const ProductRecord &product)
        {
            return archive_root / product.relative_path;
        }

        const ProductRecord *FindProduct(const SourceArchiveSnapshot &snapshot,
                                         ArchiveProductType type, const ContentHash &hash)
        {
            for (const ProductRecord &product : snapshot.products)
            {
                if (product.asset_type == type && product.content_hash == hash)
                {
                    return &product;
                }
            }
            return nullptr;
        }

        void ValidateModelReferences(const NativeModelData &data,
                                     const std::vector<SourceProductRecord> &source_products)
        {
            std::vector<const SourceProductRecord *> materials;
            for (const SourceProductRecord &source_product : source_products)
            {
                if (source_product.asset_type == ArchiveProductType::Material &&
                    source_product.role == kMaterialProductRole)
                {
                    materials.push_back(&source_product);
                }
            }
            std::sort(materials.begin(), materials.end(),
                      [](const SourceProductRecord *lhs, const SourceProductRecord *rhs)
                      { return lhs->slot < rhs->slot; });
            if (materials.size() != data.material_references.size())
            {
                Fail(ModelImportErrorCode::ProductInvalid,
                     "native model material references do not match archive slots");
            }
            for (std::size_t index = 0; index < materials.size(); ++index)
            {
                if (materials[index]->slot != static_cast<std::int32_t>(index) ||
                    materials[index]->content_hash != data.material_references[index].content_hash)
                {
                    Fail(ModelImportErrorCode::ProductInvalid,
                         "native model material reference order does not match archive slots");
                }
            }
        }

        void ValidateProducts(const std::filesystem::path &archive_root,
                              const SourceArchiveSnapshot &snapshot, ModelImportMetrics &metrics)
        {
            const SourceProductRecord *model_reference = nullptr;
            for (const SourceProductRecord &source_product : snapshot.source_products)
            {
                if (source_product.asset_type == ArchiveProductType::Model &&
                    source_product.role == kModelProductRole && source_product.slot == -1)
                {
                    model_reference = &source_product;
                    break;
                }
            }
            if (model_reference == nullptr)
            {
                Fail(ModelImportErrorCode::ProductInvalid, "archive source has no root model product");
            }
            const ProductRecord *model_product =
                FindProduct(snapshot, ArchiveProductType::Model, model_reference->content_hash);
            if (model_product == nullptr)
            {
                Fail(ModelImportErrorCode::ProductInvalid, "archive root model product is not registered");
            }
            const NativeModelProduct model =
                DeserializeNativeModel(ReadBytes(ProductPath(archive_root, *model_product),
                                                  &metrics.product_bytes_read));
            ValidateModelReferences(model.data, snapshot.source_products);

            for (const ProductRecord &product : snapshot.products)
            {
                const std::uint32_t expected_schema =
                    product.asset_type == ArchiveProductType::Model
                        ? kNativeModelVersion
                        : product.asset_type == ArchiveProductType::Material
                              ? kNativeMaterialSchemaVersion
                              : 1u;
                if (product.schema_version != expected_schema)
                {
                    Fail(ModelImportErrorCode::ProductInvalid,
                         "archive product schema metadata is invalid: " + product.relative_path);
                }
                const std::vector<std::byte> bytes =
                    ReadBytes(ProductPath(archive_root, product), &metrics.product_bytes_read);
                if (Sha256(bytes) != product.content_hash)
                {
                    Fail(ModelImportErrorCode::ProductInvalid,
                         "archive product hash changed during validation: " + product.relative_path);
                }
                if (product.asset_type == ArchiveProductType::Material)
                {
                    ValidateNativeMaterialProduct(bytes);
                }
                else if (product.asset_type == ArchiveProductType::Texture)
                {
                    try
                    {
                        (void)DeserializeNativeTexture(bytes);
                    }
                    catch (const NativeTextureError &error)
                    {
                        Fail(ModelImportErrorCode::ProductInvalid,
                             "archive texture product is invalid: " + std::string{error.what()});
                    }
                }
            }
        }

        std::optional<ModelImportResult> TryCacheHit(
            const ModelImportRequest &request, const std::filesystem::path &asset_root,
            const std::filesystem::path &archive_root, const std::string &source_relative_path,
            const ContentHash &settings_hash, std::int32_t busy_timeout_ms,
            ModelImportMetrics &metrics)
        {
            ModelArchiveDatabase archive{archive_root / "archive.sqlite3", busy_timeout_ms};
            const std::optional<SourceArchiveSnapshot> existing = archive.FindSource(source_relative_path);
            if (!existing.has_value())
            {
                return std::nullopt;
            }

            std::vector<SourceDependencyRecord> dependencies;
            try
            {
                MetricTimer timer(metrics, ModelImportMetricStage::DependencyHash);
                dependencies = HashRecordedDependencies(*existing, asset_root, metrics);
            }
            catch (const ModelArchiveError &error)
            {
                if (error.Code() == ModelArchiveErrorCode::IoError ||
                    error.Code() == ModelArchiveErrorCode::MissingProduct)
                {
                    return std::nullopt;
                }
                throw;
            }
            const ContentHash package_hash = HashSourcePackage(
                [&dependencies]
                {
                    std::vector<SourceFingerprintInput> result;
                    result.reserve(dependencies.size());
                    for (const SourceDependencyRecord &dependency : dependencies)
                    {
                        result.push_back({dependency.normalized_path, dependency.content_hash});
                    }
                    return result;
                }());
            const ArchiveProbeResult probe = archive.ProbeSource(
                {source_relative_path, package_hash, request.settings.importer_id,
                 request.settings.importer_version, settings_hash,
                 request.settings.native_model_version});
            if (probe.status != ArchiveProbeStatus::UpToDate || !probe.snapshot.has_value())
            {
                return std::nullopt;
            }
            try
            {
                MetricTimer timer(metrics, ModelImportMetricStage::ProductValidate);
                ValidateProducts(archive_root, *probe.snapshot, metrics);
            }
            catch (const ModelImportError &)
            {
                return std::nullopt;
            }

            ModelImportResult result;
            result.status = ModelImportStatus::UpToDate;
            result.metrics = metrics;
            result.metrics.cache_hit = true;
            result.metrics.cache_hit_count = 1;
            result.metrics.product_count = probe.snapshot->products.size();
            result.normalized_source_path = source_relative_path;
            result.source_package_hash = package_hash;
            for (const SourceProductRecord &source_product : probe.snapshot->source_products)
            {
                if (source_product.asset_type == ArchiveProductType::Model &&
                    source_product.role == kModelProductRole)
                {
                    result.model_hash = source_product.content_hash;
                    const ProductRecord *product =
                        FindProduct(*probe.snapshot, ArchiveProductType::Model, result.model_hash);
                    if (product != nullptr)
                    {
                        result.model_path = archive_root / product->relative_path;
                    }
                }
                else if (source_product.asset_type == ArchiveProductType::Material &&
                         source_product.role == kMaterialProductRole)
                {
                    result.material_hashes.push_back(source_product.content_hash);
                }
            }
            for (const ProductRecord &product : probe.snapshot->products)
            {
                if (product.asset_type == ArchiveProductType::Texture)
                {
                    result.texture_hashes.push_back(product.content_hash);
                }
            }
            return result;
        }

        struct PendingProduct
        {
            ProductRecord record;
            std::vector<std::byte> bytes;
        };

        void PublishProduct(const std::filesystem::path &archive_root,
                            const std::filesystem::path &operation_root,
                            const PendingProduct &product, ModelImportMetrics &metrics)
        {
            {
                MetricTimer timer(metrics, ModelImportMetricStage::ProductHash);
                if (Sha256(product.bytes) != product.record.content_hash)
                {
                    Fail(ModelImportErrorCode::ProductInvalid,
                         "staged product bytes do not match their content hash");
                }
            }
            const std::filesystem::path destination = ProductPath(archive_root, product.record);
            std::error_code error;
            if (std::filesystem::exists(destination, error) && !error)
            {
                if (!std::filesystem::is_regular_file(destination, error) || error ||
                    std::filesystem::file_size(destination, error) != product.bytes.size() || error ||
                    Sha256File(destination) != product.record.content_hash)
                {
                    Fail(ModelImportErrorCode::ProductCollision,
                         "immutable archive product collides with different bytes: " +
                             destination.string());
                }
                return;
            }
            if (error)
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to inspect archive product destination: " + error.message());
            }

            const std::filesystem::path staged = operation_root / product.record.relative_path;
            WriteBytes(staged, product.bytes, &metrics);
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to create archive product directory: " + error.message());
            }

            // A hard link is an atomic create-if-absent operation on the local
            // archive filesystems supported by the importer. It cannot replace
            // a concurrent winner like filesystem::rename can on POSIX.
            std::filesystem::create_hard_link(staged, destination, error);
            if (!error)
            {
                std::filesystem::remove(staged, error);
                return;
            }
            std::error_code destination_error;
            const bool destination_exists = std::filesystem::exists(destination, destination_error) &&
                                            !destination_error;
            if (destination_exists)
            {
                if (!std::filesystem::is_regular_file(destination, destination_error) ||
                    destination_error || std::filesystem::file_size(destination, destination_error) !=
                                            product.bytes.size() || destination_error ||
                    Sha256File(destination) != product.record.content_hash)
                {
                    Fail(ModelImportErrorCode::ProductCollision,
                         "concurrent archive product has different bytes: " + destination.string());
                }
                std::filesystem::remove(staged, error);
                return;
            }
            Fail(ModelImportErrorCode::PublicationFailed,
                 "failed to publish immutable archive product: " + error.message());
        }

        struct StagingCleanup final
        {
            std::filesystem::path path;
            ~StagingCleanup() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        };
    }

    struct ModelImportService::Impl
    {
        explicit Impl(std::int32_t timeout) : busy_timeout_ms(timeout)
        {
        }

        std::int32_t busy_timeout_ms;
        std::mutex coordination_mutex;
        std::map<std::string, std::shared_ptr<std::mutex>> source_mutexes;
        std::atomic_uint64_t operation_sequence{};
    };

    ModelImportError::ModelImportError(ModelImportErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    ModelImportErrorCode ModelImportError::Code() const noexcept
    {
        return code_;
    }

    ModelImportService::ModelImportService(std::int32_t archive_busy_timeout_ms)
        : impl_(std::make_unique<Impl>(archive_busy_timeout_ms))
    {
        if (archive_busy_timeout_ms < 0)
        {
            Fail(ModelImportErrorCode::InvalidArgument,
                 "archive busy timeout cannot be negative");
        }
    }

    ModelImportService::~ModelImportService() noexcept = default;

    ModelImportResult ModelImportService::Import(const ModelImportRequest &request)
    {
        const Clock::time_point import_started = Clock::now();
        const ProcessSnapshot process_started = ReadProcessSnapshot();
        ModelImportMetrics metrics{};
        metrics.peak_active_jobs = 1;
        const std::filesystem::path asset_root = AbsoluteNormalized(request.asset_root);
        const std::filesystem::path source_path = ResolveSourcePath(request, asset_root);
        const std::string source_relative_path = AssetRelativePath(asset_root, source_path);
        const std::filesystem::path archive_root =
            request.archive_root.empty() ? asset_root / ".archive" : AbsoluteNormalized(request.archive_root);
        const ContentHash settings_hash = SettingsHash(request.settings);

        std::shared_ptr<std::mutex> source_mutex;
        {
            std::lock_guard<std::mutex> lock(impl_->coordination_mutex);
            auto &entry = impl_->source_mutexes[source_relative_path];
            if (!entry)
            {
                entry = std::make_shared<std::mutex>();
            }
            source_mutex = entry;
        }
        std::lock_guard<std::mutex> source_lock(*source_mutex);

        ReportProgress(request, ModelImportProgressStage::CheckingCache,
                       "checking archive cache for " + source_relative_path);

        std::error_code error;
        std::filesystem::create_directories(archive_root, error);
        if (error)
        {
            Fail(ModelImportErrorCode::PublicationFailed,
                 "failed to create archive root: " + error.message());
        }

        try
        {
            std::optional<ModelImportResult> hit;
            {
                MetricTimer timer(metrics, ModelImportMetricStage::CacheProbe);
                hit = TryCacheHit(request, asset_root, archive_root, source_relative_path,
                                  settings_hash, impl_->busy_timeout_ms, metrics);
            }
            if (hit.has_value())
            {
                FinalizeMetrics(hit->metrics, import_started, process_started);
                ReportProgress(request, ModelImportProgressStage::Complete,
                               "cache hit; native products are up to date");
                return std::move(*hit);
            }
        }
        catch (const ModelArchiveError &error)
        {
            Fail(error.Code() == ModelArchiveErrorCode::ArchiveBusy
                     ? ModelImportErrorCode::ArchiveBusy
                     : ModelImportErrorCode::ArchiveCommitFailed,
                 error.what());
        }

        ImportedModelDocument document;
        try
        {
            ReportProgress(request, ModelImportProgressStage::DecodingSource,
                           "decoding source model " + source_path.filename().string());
            MetricTimer timer(metrics, ModelImportMetricStage::SourceDecode);
            AssimpModelDecoder decoder;
            document = decoder.Decode(source_path);
            ReportProgress(request, ModelImportProgressStage::DecodingSource,
                           "decoded " + std::to_string(document.materials.size()) +
                               " materials and " + std::to_string(document.images.size()) + " images");
        }
        catch (const ImportedModelDecodeError &error)
        {
            Fail(ModelImportErrorCode::DecodeFailed, error.what());
        }

        std::vector<SourceDependencyRecord> dependencies;
        try
        {
            ReportProgress(request, ModelImportProgressStage::HashingDependencies,
                           "hashing source dependencies");
            MetricTimer timer(metrics, ModelImportMetricStage::DependencyHash);
            dependencies = HashDependencies(document, asset_root, source_path, source_relative_path,
                                             metrics);
            ReportProgress(request, ModelImportProgressStage::HashingDependencies,
                           "hashed " + std::to_string(dependencies.size()) + " dependencies");
        }
        catch (const ModelArchiveError &error)
        {
            Fail(ModelImportErrorCode::IoError, error.what());
        }
        const ContentHash package_hash = HashSourcePackage(
            [&dependencies]
            {
                std::vector<SourceFingerprintInput> result;
                result.reserve(dependencies.size());
                for (const SourceDependencyRecord &dependency : dependencies)
                {
                    result.push_back({dependency.normalized_path, dependency.content_hash});
                }
                return result;
            }());

        NativeMaterialConversionResult converted_materials;
        try
        {
            ReportProgress(request, ModelImportProgressStage::CookingTextures,
                           "cooking material textures");
            MetricTimer timer(metrics, ModelImportMetricStage::TextureCook);
            NativeMaterialConversionSettings conversion_settings{
                asset_root, request.settings.shader_asset_path, request.settings.texture_settings,
                request.settings.emit_texture_profile_variants};
            conversion_settings.texture_progress_callback = [&request](std::string_view image_path)
            {
                ReportProgress(request, ModelImportProgressStage::CookingTextures,
                               "cooking texture " + std::string{image_path});
            };
            converted_materials = ConvertImportedMaterials(document, conversion_settings);
            ReportProgress(request, ModelImportProgressStage::CookingTextures,
                           "cooked " + std::to_string(converted_materials.embedded_images.size()) +
                               " native texture products");
        }
        catch (const NativeMaterialConversionError &error)
        {
            Fail(ModelImportErrorCode::ConversionFailed, error.what());
        }

        NativeModelData model_data;
        model_data.vertices = std::move(document.mesh.vertices);
        model_data.indices = std::move(document.mesh.indices);
        model_data.sections = std::move(document.mesh.sections);
        model_data.local_bounds = document.mesh.local_bounds;
        model_data.material_references.reserve(converted_materials.materials.size());
        for (const NativeMaterialProduct &material : converted_materials.materials)
        {
            model_data.material_references.push_back({AssetType::KPAT_Material,
                                                      material.content_hash});
        }

        std::vector<std::byte> model_bytes;
        try
        {
            ReportProgress(request, ModelImportProgressStage::SerializingProducts,
                           "serializing native model and material products");
            {
                MetricTimer timer(metrics, ModelImportMetricStage::ProductSerialize);
                model_bytes = SerializeNativeModel(model_data);
            }
            {
                MetricTimer timer(metrics, ModelImportMetricStage::ProductValidate);
                const NativeModelProduct decoded = DeserializeNativeModel(model_bytes);
                bool metadata_matches =
                    decoded.data.vertices.size() <= model_data.vertices.size() &&
                    decoded.data.indices.size() == model_data.indices.size() &&
                    decoded.data.sections.size() == model_data.sections.size() &&
                    decoded.data.material_references == model_data.material_references &&
                    decoded.data.local_bounds == model_data.local_bounds;
                if (metadata_matches)
                {
                    for (std::size_t index = 0; index < decoded.data.sections.size(); ++index)
                    {
                        const data::MeshSection &decoded_section = decoded.data.sections[index];
                        const data::MeshSection &source_section = model_data.sections[index];
                        if (decoded_section.index_start != source_section.index_start ||
                            decoded_section.index_count != source_section.index_count ||
                            decoded_section.material_index != source_section.material_index ||
                            decoded_section.local_bounds != source_section.local_bounds)
                        {
                            metadata_matches = false;
                            break;
                        }
                    }
                }
                if (!metadata_matches)
                {
                    Fail(ModelImportErrorCode::ProductInvalid,
                         "serialized native model failed its topology/metadata round-trip validation");
                }
                for (const NativeMaterialProduct &material : converted_materials.materials)
                {
                    ValidateNativeMaterialProduct(material.bytes);
                }
                for (const NativeImageProduct &image : converted_materials.embedded_images)
                {
                    try
                    {
                        (void)DeserializeNativeTexture(image.bytes);
                    }
                    catch (const NativeTextureError &error)
                    {
                        Fail(ModelImportErrorCode::ProductInvalid,
                             "serialized texture failed validation: " + std::string{error.what()});
                    }
                }
            }
        }
        catch (const NativeModelError &error)
        {
            Fail(ModelImportErrorCode::ProductInvalid, error.what());
        }
        catch (const NativeMaterialConversionError &error)
        {
            Fail(ModelImportErrorCode::ProductInvalid, error.what());
        }

        std::vector<PendingProduct> products;
        ContentHash model_hash{};
        {
            MetricTimer timer(metrics, ModelImportMetricStage::ProductHash);
            model_hash = Sha256(model_bytes);
        }
        products.push_back({{model_hash, ArchiveProductType::Model,
                             ProductRelativePath(ArchiveProductType::Model, model_hash),
                             static_cast<std::uint64_t>(model_bytes.size()),
                             request.settings.native_model_version},
                            std::move(model_bytes)});
        std::vector<SourceProductRecord> source_products;
        source_products.push_back({model_hash, ArchiveProductType::Model, kModelProductRole, -1,
                                   source_path.stem().string()});

        std::vector<ContentHash> material_hashes;
        material_hashes.reserve(converted_materials.materials.size());
        for (std::size_t index = 0; index < converted_materials.materials.size(); ++index)
        {
            const NativeMaterialProduct &material = converted_materials.materials[index];
            material_hashes.push_back(material.content_hash);
            products.push_back({{material.content_hash, ArchiveProductType::Material,
                                 ProductRelativePath(ArchiveProductType::Material,
                                                      material.content_hash),
                                 static_cast<std::uint64_t>(material.bytes.size()),
                                 request.settings.material_schema_version},
                                material.bytes});
            const std::string display_name = material.display_name.empty()
                                                  ? "Material_" + std::to_string(index)
                                                  : material.display_name;
            source_products.push_back({material.content_hash, ArchiveProductType::Material,
                                       kMaterialProductRole, static_cast<std::int32_t>(index),
                                       display_name});
        }

        std::vector<ContentHash> texture_hashes;
        for (const NativeImageProduct &image : converted_materials.embedded_images)
        {
            texture_hashes.push_back(image.content_hash);
            products.push_back({{image.content_hash, ArchiveProductType::Texture,
                                 ProductRelativePath(ArchiveProductType::Texture,
                                                      image.content_hash, "texture"),
                                 static_cast<std::uint64_t>(image.bytes.size()), 1},
                                image.bytes});
        }

        const std::filesystem::path operation_root =
            archive_root / "staging" /
            (source_path.stem().string() + "-" + std::to_string(
                impl_->operation_sequence.fetch_add(1, std::memory_order_relaxed)));
        StagingCleanup cleanup{operation_root};
        {
            MetricTimer timer(metrics, ModelImportMetricStage::StagingWrite);
            for (std::size_t index = 0; index < products.size(); ++index)
            {
                const PendingProduct &product = products[index];
                ReportProgress(request, ModelImportProgressStage::PublishingProducts,
                               "staging " + product.record.relative_path,
                               index + 1, products.size());
                WriteBytes(operation_root / product.record.relative_path, product.bytes, &metrics);
            }
        }
        {
            MetricTimer timer(metrics, ModelImportMetricStage::Publication);
            for (std::size_t index = 0; index < products.size(); ++index)
            {
                const PendingProduct &product = products[index];
                ReportProgress(request, ModelImportProgressStage::PublishingProducts,
                               "publishing " + product.record.relative_path,
                               index + 1, products.size());
                PublishProduct(archive_root, operation_root, product, metrics);
            }
        }

        SourceRecord source;
        source.normalized_path = source_relative_path;
        source.path_hash = Sha256(source_relative_path);
        source.display_name = source_path.stem().string();
        source.package_hash = package_hash;
        source.importer_id = request.settings.importer_id;
        source.importer_version = request.settings.importer_version;
        source.settings_hash = settings_hash;
        source.native_model_version = request.settings.native_model_version;
        source.status = SourceImportStatus::Ready;

        std::vector<ProductRecord> product_records;
        product_records.reserve(products.size());
        for (const PendingProduct &product : products)
        {
            product_records.push_back(product.record);
        }

        try
        {
            ReportProgress(request, ModelImportProgressStage::UpdatingArchive,
                           "updating archive index");
            MetricTimer timer(metrics, ModelImportMetricStage::ArchiveCommit);
            ModelArchiveDatabase archive{archive_root / "archive.sqlite3", impl_->busy_timeout_ms};
            archive.ReplaceSource(source, dependencies, product_records, source_products, {});
        }
        catch (const ModelArchiveError &error)
        {
            Fail(error.Code() == ModelArchiveErrorCode::ArchiveBusy
                     ? ModelImportErrorCode::ArchiveBusy
                     : ModelImportErrorCode::ArchiveCommitFailed,
                 error.what());
        }

        ModelImportResult result;
        result.status = ModelImportStatus::Imported;
        result.normalized_source_path = source_relative_path;
        result.source_package_hash = package_hash;
        result.model_hash = model_hash;
        result.model_path = archive_root / ProductRelativePath(ArchiveProductType::Model, model_hash);
        result.material_hashes = std::move(material_hashes);
        result.texture_hashes = std::move(texture_hashes);
        result.metrics = metrics;
        result.metrics.source_image_count = document.images.size();
        result.metrics.product_count = products.size();
        result.metrics.unique_cook_keys = converted_materials.metrics.unique_cook_keys;
        result.metrics.requested_texture_bindings =
            converted_materials.metrics.requested_texture_bindings;
        result.metrics.texture_decode_count = converted_materials.metrics.texture_decode_count;
        result.metrics.texture_cook_count = converted_materials.metrics.texture_cook_count;
        result.metrics.portable_encode_count = converted_materials.metrics.portable_encode_count;
        result.metrics.block_encode_count = converted_materials.metrics.block_encode_count;
        result.metrics.unique_texture_product_count =
            converted_materials.metrics.unique_texture_product_count;
        result.metrics.texture_product_bytes = converted_materials.metrics.texture_product_bytes;
        result.metrics.cache_hit = false;
        FinalizeMetrics(result.metrics, import_started, process_started);
        ReportProgress(request, ModelImportProgressStage::Complete,
                       "import completed: " + std::to_string(products.size()) + " products");
        return result;
    }

    ModelImportResult ImportModel(const ModelImportRequest &request)
    {
        return ModelImportService{}.Import(request);
    }
}
