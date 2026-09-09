#include "model_import_service.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
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
#include "image_io/image_io.h"

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
            ImportedModelDocument &document, const std::filesystem::path &asset_root,
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
            for (ImportedImageSource &image : document.images)
            {
                if (image.storage == ImportedImageStorage::EmbeddedBytes)
                {
                    image.source_hash = Sha256(image.embedded_bytes);
                    continue;
                }
                if (!image.resolved_path.empty())
                {
                    const std::string normalized = AssetRelativePath(asset_root, image.resolved_path);
                    const auto found = unique.find(normalized);
                    if (found != unique.end())
                    {
                        image.source_hash = found->second;
                    }
                }
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

        struct StagedProduct
        {
            ProductRecord record;
            std::filesystem::path staged_path;
        };

        void PublishProduct(const std::filesystem::path &archive_root,
                            const StagedProduct &product)
        {
            const std::filesystem::path destination = ProductPath(archive_root, product.record);
            std::error_code error;
            if (std::filesystem::exists(destination, error) && !error)
            {
                if (!std::filesystem::is_regular_file(destination, error) || error ||
                    std::filesystem::file_size(destination, error) != product.record.byte_size || error ||
                    Sha256File(destination) != product.record.content_hash)
                {
                    Fail(ModelImportErrorCode::ProductCollision,
                         "immutable archive product collides with different bytes: " +
                             destination.string());
                }
                std::filesystem::remove(product.staged_path, error);
                return;
            }
            if (error)
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to inspect archive product destination: " + error.message());
            }

            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to create archive product directory: " + error.message());
            }

            // A hard link is an atomic create-if-absent operation on the local
            // archive filesystems supported by the importer. It cannot replace
            // a concurrent winner like filesystem::rename can on POSIX.
            std::filesystem::create_hard_link(product.staged_path, destination, error);
            if (!error)
            {
                std::filesystem::remove(product.staged_path, error);
                return;
            }
            std::error_code destination_error;
            const bool destination_exists = std::filesystem::exists(destination, destination_error) &&
                                            !destination_error;
            if (destination_exists)
            {
                if (!std::filesystem::is_regular_file(destination, destination_error) ||
                    destination_error || std::filesystem::file_size(destination, destination_error) !=
                                            product.record.byte_size || destination_error ||
                    Sha256File(destination) != product.record.content_hash)
                {
                    Fail(ModelImportErrorCode::ProductCollision,
                         "concurrent archive product has different bytes: " + destination.string());
                }
                std::filesystem::remove(product.staged_path, error);
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

        class PipelineStop final
        {
        public:
            explicit PipelineStop(std::function<bool()> cancellation_requested)
                : cancellation_requested_(std::move(cancellation_requested))
            {
            }

            bool IsRequested() const noexcept
            {
                return requested_.load(std::memory_order_acquire);
            }

            bool CheckCancellation()
            {
                if (IsRequested()) return true;
                std::lock_guard<std::mutex> lock(cancellation_mutex_);
                if (IsRequested()) return true;
                if (cancellation_requested_ && cancellation_requested_())
                {
                    requested_.store(true, std::memory_order_release);
                    return true;
                }
                return false;
            }

            void Request() noexcept
            {
                requested_.store(true, std::memory_order_release);
            }

        private:
            std::atomic_bool requested_{false};
            std::mutex cancellation_mutex_;
            std::function<bool()> cancellation_requested_;
        };

        class MemoryBudget;

        class MemoryReservation final
        {
        public:
            MemoryReservation() = default;
            MemoryReservation(MemoryBudget *owner, std::uint64_t bytes, bool oversized) noexcept
                : owner_(owner), bytes_(bytes), oversized_(oversized)
            {
            }

            ~MemoryReservation() noexcept;

            MemoryReservation(const MemoryReservation &) = delete;
            MemoryReservation &operator=(const MemoryReservation &) = delete;

            MemoryReservation(MemoryReservation &&other) noexcept
                : owner_(other.owner_), bytes_(other.bytes_), oversized_(other.oversized_)
            {
                other.owner_ = nullptr;
                other.bytes_ = 0;
                other.oversized_ = false;
            }

            MemoryReservation &operator=(MemoryReservation &&other) noexcept;

            std::uint64_t bytes() const noexcept { return bytes_; }

        private:
            void Reset() noexcept;

            MemoryBudget *owner_{};
            std::uint64_t bytes_{};
            bool oversized_{false};
        };

        class MemoryBudget final
        {
        public:
            explicit MemoryBudget(std::uint64_t budget_bytes) : budget_bytes_(budget_bytes)
            {
            }

            std::optional<MemoryReservation> Acquire(std::uint64_t estimate,
                                                       PipelineStop &stop,
                                                       std::uint64_t &wait_nanoseconds)
            {
                const auto started = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                for (;;)
                {
                    const std::uint64_t current = current_bytes_.load(std::memory_order_relaxed);
                    const bool fits = estimate <= budget_bytes_ &&
                                       current <= budget_bytes_ - estimate;
                    const bool oversized = estimate > budget_bytes_ && current == 0 &&
                                           !oversized_active_;
                    if (!stop.IsRequested() && (fits || oversized))
                    {
                        current_bytes_ += estimate;
                        const std::uint64_t reserved =
                            current_bytes_.load(std::memory_order_relaxed);
                        std::uint64_t peak = peak_bytes_.load(std::memory_order_relaxed);
                        while (peak < reserved &&
                               !peak_bytes_.compare_exchange_weak(
                                   peak, reserved,
                                   std::memory_order_relaxed))
                        {
                        }
                        if (oversized)
                        {
                            oversized_active_ = true;
                            ++oversized_count_;
                        }
                        wait_nanoseconds += ElapsedNanoseconds(started);
                        return MemoryReservation{this, estimate, oversized};
                    }
                    if (stop.IsRequested())
                    {
                        wait_nanoseconds += ElapsedNanoseconds(started);
                        return std::nullopt;
                    }
                    cv_.wait_for(lock, std::chrono::milliseconds{25});
                    lock.unlock();
                    const bool cancelled = stop.CheckCancellation();
                    lock.lock();
                    if (cancelled)
                    {
                        wait_nanoseconds += ElapsedNanoseconds(started);
                        return std::nullopt;
                    }
                }
            }

            void Notify() noexcept
            {
                cv_.notify_all();
            }

            std::uint64_t current_bytes() const noexcept { return current_bytes_.load(); }
            std::uint64_t peak_bytes() const noexcept { return peak_bytes_.load(); }
            std::uint64_t oversized_count() const noexcept { return oversized_count_.load(); }

        private:
            friend class MemoryReservation;

            void Release(std::uint64_t bytes, bool oversized) noexcept
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    current_bytes_ -= bytes;
                    if (oversized) oversized_active_ = false;
                }
                cv_.notify_all();
            }

            static std::uint64_t ElapsedNanoseconds(Clock::time_point started) noexcept
            {
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started)
                        .count());
            }

            const std::uint64_t budget_bytes_;
            mutable std::mutex mutex_;
            std::condition_variable cv_;
            std::atomic<std::uint64_t> current_bytes_{0};
            std::atomic<std::uint64_t> peak_bytes_{0};
            std::atomic<std::uint64_t> oversized_count_{0};
            bool oversized_active_{false};
        };

        void MemoryReservation::Reset() noexcept
        {
            if (owner_ != nullptr)
            {
                owner_->Release(bytes_, oversized_);
                owner_ = nullptr;
                bytes_ = 0;
                oversized_ = false;
            }
        }

        MemoryReservation::~MemoryReservation() noexcept
        {
            Reset();
        }

        MemoryReservation &MemoryReservation::operator=(MemoryReservation &&other) noexcept
        {
            if (this != &other)
            {
                Reset();
                owner_ = other.owner_;
                bytes_ = other.bytes_;
                oversized_ = other.oversized_;
                other.owner_ = nullptr;
                other.bytes_ = 0;
                other.oversized_ = false;
            }
            return *this;
        }

        struct TextureCompletion final
        {
            NativeTextureCookResult result;
            MemoryReservation reservation;
        };

        class CompletionQueue final
        {
        public:
            CompletionQueue(std::size_t capacity, std::size_t worker_count, PipelineStop &stop)
                : capacity_(capacity), live_workers_(worker_count), stop_(stop)
            {
            }

            bool Push(TextureCompletion completion, std::uint64_t &wait_nanoseconds)
            {
                const auto started = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                while (queue_.size() >= capacity_ && !stop_.IsRequested())
                {
                    cv_.wait_for(lock, std::chrono::milliseconds{25});
                    lock.unlock();
                    const bool cancelled = stop_.CheckCancellation();
                    lock.lock();
                    if (cancelled) break;
                }
                wait_nanoseconds += ElapsedNanoseconds(started);
                if (stop_.IsRequested()) return false;
                queue_.push_back(std::move(completion));
                const std::size_t size = queue_.size();
                std::size_t peak = peak_size_.load(std::memory_order_relaxed);
                while (peak < size &&
                       !peak_size_.compare_exchange_weak(peak, size,
                                                         std::memory_order_relaxed))
                {
                }
                cv_.notify_all();
                return true;
            }

            std::optional<TextureCompletion> Pop(std::uint64_t &wait_nanoseconds)
            {
                const auto started = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                while (queue_.empty() && !closed_ && !stop_.IsRequested())
                {
                    cv_.wait_for(lock, std::chrono::milliseconds{25});
                    lock.unlock();
                    const bool cancelled = stop_.CheckCancellation();
                    lock.lock();
                    if (cancelled) break;
                }
                wait_nanoseconds += ElapsedNanoseconds(started);
                if (queue_.empty() || stop_.IsRequested()) return std::nullopt;
                TextureCompletion completion = std::move(queue_.front());
                queue_.pop_front();
                cv_.notify_all();
                return completion;
            }

            void WorkerFinished() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (live_workers_ > 0) --live_workers_;
                if (live_workers_ == 0) closed_ = true;
                cv_.notify_all();
            }

            void Fail(std::exception_ptr error) noexcept
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (first_error_ == nullptr) first_error_ = std::move(error);
                }
                stop_.Request();
                cv_.notify_all();
            }

            void Notify() noexcept { cv_.notify_all(); }

            std::exception_ptr FirstError() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return first_error_;
            }

            std::size_t peak_size() const noexcept { return peak_size_.load(); }

        private:
            static std::uint64_t ElapsedNanoseconds(Clock::time_point started) noexcept
            {
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started)
                        .count());
            }

            const std::size_t capacity_;
            mutable std::mutex mutex_;
            std::condition_variable cv_;
            std::deque<TextureCompletion> queue_;
            std::size_t live_workers_{};
            bool closed_{false};
            std::exception_ptr first_error_;
            PipelineStop &stop_;
            std::atomic<std::size_t> peak_size_{0};
        };

        struct ResolvedExecutionPolicy final
        {
            std::uint32_t worker_count{};
            std::uint64_t memory_budget_bytes{};
            std::uint32_t completion_queue_capacity{};
        };

        ResolvedExecutionPolicy ResolveExecutionPolicy(const ModelImportExecutionPolicy &policy)
        {
            const unsigned logical_cpus = std::max(1u, std::thread::hardware_concurrency());
            const std::uint32_t automatic_workers = static_cast<std::uint32_t>(
                std::clamp(logical_cpus > 1 ? logical_cpus - 1 : 1u, 1u, 8u));
            if (policy.texture_worker_count > 64 || policy.completion_queue_capacity == 0 ||
                policy.completion_queue_capacity > 1024)
            {
                Fail(ModelImportErrorCode::InvalidArgument,
                     "texture worker count or completion queue capacity is invalid");
            }
            constexpr std::uint64_t kDefaultTextureBudget = 1024ull * 1024ull * 1024ull;
            return {policy.texture_worker_count == 0 ? automatic_workers
                                                     : policy.texture_worker_count,
                    policy.texture_memory_budget_bytes == 0 ? kDefaultTextureBudget
                                                              : policy.texture_memory_budget_bytes,
                    policy.completion_queue_capacity};
        }

        std::uint64_t CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs,
                                      const std::string &description)
        {
            if (rhs != 0 && lhs > std::numeric_limits<std::uint64_t>::max() / rhs)
            {
                Fail(ModelImportErrorCode::InvalidArgument,
                     "texture memory estimate overflow: " + description);
            }
            return lhs * rhs;
        }

        std::uint64_t CheckedAdd(std::uint64_t lhs, std::uint64_t rhs,
                                 const std::string &description)
        {
            if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs)
            {
                Fail(ModelImportErrorCode::InvalidArgument,
                     "texture memory estimate overflow: " + description);
            }
            return lhs + rhs;
        }

        std::uint64_t TextureDecodedByteCount(const ImportedImageSource &image)
        {
            if (image.storage == ImportedImageStorage::EmbeddedBytes && image.embedded_is_raw_rgba8)
            {
                return CheckedMultiply(CheckedMultiply(image.embedded_width, image.embedded_height,
                                                       image.path),
                                       4, image.path);
            }
            const image_io::ImageMetadataResult metadata =
                image.storage == ImportedImageStorage::ExternalFile
                    ? image_io::ProbeImageFile(image.resolved_path.string())
                    : image_io::ProbeImageMemory(image.embedded_bytes);
            if (!metadata.result.success)
            {
                Fail(ModelImportErrorCode::DecodeFailed,
                     "image metadata probe failed: " + image.path + ": " + metadata.result.diagnostic);
            }
            return metadata.metadata.decoded_byte_count;
        }

        std::uint64_t EstimateTextureJob(const ImportedModelDocument &document,
                                         const NativeTextureCookJob &job)
        {
            if (job.image_index >= document.images.size())
            {
                Fail(ModelImportErrorCode::InvalidArgument, "texture cook job image index is invalid");
            }
            const std::uint64_t decoded = TextureDecodedByteCount(document.images[job.image_index]);
            constexpr std::uint64_t kWorkingSetMultiplier = 5;
            constexpr std::uint64_t kCookScratchBytes = 4ull * 1024ull * 1024ull;
            return CheckedAdd(CheckedMultiply(decoded, kWorkingSetMultiplier,
                                               document.images[job.image_index].path),
                              kCookScratchBytes, document.images[job.image_index].path);
        }

        std::filesystem::path CreateOperationRoot(const std::filesystem::path &archive_root,
                                                  const std::string &source_stem,
                                                  std::uint64_t sequence)
        {
            const std::filesystem::path staging_root = archive_root / "staging";
            std::error_code error;
            std::filesystem::create_directories(staging_root, error);
            if (error)
            {
                Fail(ModelImportErrorCode::PublicationFailed,
                     "failed to create staging root: " + error.message());
            }
            const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
            for (std::uint32_t attempt = 0; attempt < 32; ++attempt)
            {
                const std::filesystem::path candidate =
                    staging_root / (source_stem + "-" + std::to_string(sequence) + "-" +
                                    std::to_string(timestamp) + "-" + std::to_string(thread_hash) +
                                    "-" + std::to_string(attempt));
                error.clear();
                if (std::filesystem::create_directory(candidate, error))
                {
                    return candidate;
                }
                if (error != std::make_error_code(std::errc::file_exists))
                {
                    Fail(ModelImportErrorCode::PublicationFailed,
                         "failed to create operation staging directory: " + error.message());
                }
            }
            Fail(ModelImportErrorCode::PublicationFailed,
                 "failed to create a unique operation staging directory");
        }

        void StageTextureProduct(const std::filesystem::path &operation_root,
                                 NativeImageProduct product,
                                 std::set<ContentHash> &staged_hashes,
                                 std::vector<StagedProduct> &staged_products,
                                 ModelImportMetrics &metrics)
        {
            if (!staged_hashes.insert(product.content_hash).second)
            {
                return;
            }
            ProductRecord record{product.content_hash, ArchiveProductType::Texture,
                                 ProductRelativePath(ArchiveProductType::Texture,
                                                      product.content_hash, "texture"),
                                 static_cast<std::uint64_t>(product.bytes.size()), 1};
            const std::filesystem::path staged_path = operation_root / record.relative_path;
            {
                MetricTimer timer(metrics, ModelImportMetricStage::StagingWrite);
                WriteBytes(staged_path, product.bytes, &metrics);
            }
            metrics.texture_product_bytes += record.byte_size;
            ++metrics.unique_texture_product_count;
            staged_products.push_back({record, staged_path});
        }

        StagedProduct StageProductBytes(const std::filesystem::path &operation_root,
                                        ProductRecord record,
                                        const std::vector<std::byte> &bytes,
                                        ModelImportMetrics &metrics)
        {
            const std::filesystem::path staged_path = operation_root / record.relative_path;
            {
                MetricTimer timer(metrics, ModelImportMetricStage::StagingWrite);
                WriteBytes(staged_path, bytes, &metrics);
            }
            return {std::move(record), staged_path};
        }

        void RunTexturePipeline(const ImportedModelDocument &document,
                                const NativeMaterialCookPlan &plan,
                                const NativeMaterialConversionSettings &settings,
                                const ModelImportRequest &request,
                                const ResolvedExecutionPolicy &policy,
                                const std::filesystem::path &operation_root,
                                std::vector<StagedProduct> &staged_texture_products,
                                std::vector<NativeTextureCookResult> &texture_results,
                                ModelImportMetrics &metrics)
        {
            const std::size_t job_count = plan.texture_jobs.size();
            texture_results.resize(job_count);
            metrics.texture_worker_count = policy.worker_count;
            metrics.completion_queue_capacity = policy.completion_queue_capacity;
            metrics.texture_memory_budget_bytes = policy.memory_budget_bytes;
            metrics.has_memory_budget = true;
            metrics.total_texture_jobs = job_count;
            std::vector<std::uint64_t> estimates;
            estimates.reserve(job_count);
            for (const NativeTextureCookJob &job : plan.texture_jobs)
            {
                const std::uint64_t estimate = EstimateTextureJob(document, job);
                estimates.push_back(estimate);
                metrics.estimated_texture_bytes =
                    CheckedAdd(metrics.estimated_texture_bytes, estimate, "all texture jobs");
            }
            if (job_count == 0)
            {
                return;
            }

            PipelineStop stop{request.execution.cancellation_requested};
            MemoryBudget budget{policy.memory_budget_bytes};
            CompletionQueue completions{policy.completion_queue_capacity, policy.worker_count, stop};
            std::atomic<std::size_t> next_job{0};
            std::atomic<std::size_t> active_jobs{0};
            std::atomic<std::size_t> peak_active_jobs{0};
            std::atomic<std::uint64_t> memory_wait_nanoseconds{0};
            std::atomic<std::uint64_t> queue_wait_nanoseconds{0};
            std::vector<std::thread> workers;
            workers.reserve(policy.worker_count);
            const auto stop_and_wake = [&]
            {
                stop.Request();
                budget.Notify();
                completions.Notify();
            };
            const auto join_workers = [&]
            {
                for (std::thread &worker : workers)
                {
                    if (worker.joinable()) worker.join();
                }
            };
            const auto worker_main = [&]
            {
                bool active_job = false;
                try
                {
                    for (;;)
                    {
                        if (stop.CheckCancellation())
                        {
                            stop_and_wake();
                            break;
                        }
                        const std::size_t ordinal = next_job.fetch_add(1, std::memory_order_relaxed);
                        if (ordinal >= job_count) break;
                        if (stop.CheckCancellation())
                        {
                            stop_and_wake();
                            break;
                        }
                        std::uint64_t memory_wait = 0;
                        std::optional<MemoryReservation> reservation =
                            budget.Acquire(estimates[ordinal], stop, memory_wait);
                        memory_wait_nanoseconds.fetch_add(memory_wait, std::memory_order_relaxed);
                        if (!reservation.has_value()) break;
                        if (stop.CheckCancellation())
                        {
                            stop_and_wake();
                            break;
                        }
                        const std::size_t active =
                            active_jobs.fetch_add(1, std::memory_order_relaxed) + 1;
                        active_job = true;
                        std::size_t peak = peak_active_jobs.load(std::memory_order_relaxed);
                        while (peak < active &&
                               !peak_active_jobs.compare_exchange_weak(
                                   peak, active, std::memory_order_relaxed))
                        {
                        }
                        NativeTextureCookResult result = ExecuteNativeTextureCookJob(
                            document, settings, plan.texture_jobs[ordinal], ordinal);
                        result.estimated_bytes = estimates[ordinal];
                        for (const NativeImageProduct &product : result.products)
                        {
                            result.actual_bytes = CheckedAdd(result.actual_bytes,
                                                             product.bytes.size(),
                                                             "texture completion payload");
                        }
                        active_jobs.fetch_sub(1, std::memory_order_relaxed);
                        active_job = false;
                        TextureCompletion completion{std::move(result), std::move(*reservation)};
                        std::uint64_t queue_wait = 0;
                        if (!completions.Push(std::move(completion), queue_wait))
                        {
                            queue_wait_nanoseconds.fetch_add(queue_wait, std::memory_order_relaxed);
                            break;
                        }
                        queue_wait_nanoseconds.fetch_add(queue_wait, std::memory_order_relaxed);
                    }
                }
                catch (...)
                {
                    if (active_job)
                    {
                        active_jobs.fetch_sub(1, std::memory_order_relaxed);
                    }
                    completions.Fail(std::current_exception());
                    budget.Notify();
                }
                completions.WorkerFinished();
            };

            try
            {
                for (std::uint32_t index = 0; index < policy.worker_count; ++index)
                {
                    workers.emplace_back(worker_main);
                }
                std::set<ContentHash> staged_hashes;
                std::size_t completed = 0;
                while (completed < job_count && !stop.IsRequested())
                {
                    std::uint64_t coordinator_wait = 0;
                    std::optional<TextureCompletion> completion = completions.Pop(coordinator_wait);
                    metrics.coordinator_wait_seconds +=
                        static_cast<double>(coordinator_wait) / 1.0e9;
                    if (!completion.has_value()) break;
                    NativeTextureCookResult &result = completion->result;
                    if (result.job_ordinal >= texture_results.size() ||
                        !texture_results[result.job_ordinal].portable_path.empty())
                    {
                        Fail(ModelImportErrorCode::ProductInvalid,
                             "texture completion has an invalid or duplicate job ordinal");
                    }
                    ReportProgress(request, ModelImportProgressStage::CookingTextures,
                                   "cooking texture " +
                                       document.images[plan.texture_jobs[result.job_ordinal].image_index].path,
                                   completed, job_count);
                    for (NativeImageProduct &product : result.products)
                    {
                        StageTextureProduct(operation_root, std::move(product), staged_hashes,
                                            staged_texture_products, metrics);
                    }
                    metrics.actual_texture_bytes =
                        CheckedAdd(metrics.actual_texture_bytes, result.actual_bytes,
                                   "texture completion payloads");
                    if (result.actual_bytes > result.estimated_bytes)
                    {
                        ++metrics.memory_estimate_correction_count;
                    }
                    texture_results[result.job_ordinal] = std::move(result);
                    texture_results[result.job_ordinal].products.clear();
                    ++completed;
                    metrics.completed_texture_jobs = completed;
                    ReportProgress(request, ModelImportProgressStage::CookingTextures,
                                   "cooked texture job " + std::to_string(completed) + "/" +
                                       std::to_string(job_count), completed, job_count);
                }
                if (stop.CheckCancellation())
                {
                    stop_and_wake();
                    Fail(ModelImportErrorCode::Cancelled, "model import was cancelled");
                }
                join_workers();
                if (const std::exception_ptr error = completions.FirstError())
                {
                    std::rethrow_exception(error);
                }
                if (metrics.completed_texture_jobs != job_count)
                {
                    Fail(ModelImportErrorCode::ConversionFailed,
                         "texture workers stopped before completing the cook plan");
                }
            }
            catch (...)
            {
                stop_and_wake();
                join_workers();
                throw;
            }
            metrics.worker_memory_wait_seconds =
                static_cast<double>(memory_wait_nanoseconds.load()) / 1.0e9;
            metrics.worker_queue_wait_seconds =
                static_cast<double>(queue_wait_nanoseconds.load()) / 1.0e9;
            metrics.peak_reserved_bytes = budget.peak_bytes();
            metrics.current_reserved_bytes = budget.current_bytes();
            metrics.oversized_job_count = budget.oversized_count();
            metrics.peak_completion_queue_size = completions.peak_size();
            metrics.peak_active_jobs = peak_active_jobs.load();
        }
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
        const ResolvedExecutionPolicy execution_policy = ResolveExecutionPolicy(request.execution);
        metrics.texture_worker_count = execution_policy.worker_count;
        metrics.completion_queue_capacity = execution_policy.completion_queue_capacity;
        metrics.texture_memory_budget_bytes = execution_policy.memory_budget_bytes;
        metrics.has_memory_budget = true;
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

        NativeMaterialConversionSettings conversion_settings{
            asset_root, request.settings.shader_asset_path, request.settings.texture_settings,
            request.settings.emit_texture_profile_variants};
        NativeMaterialCookPlan cook_plan;
        try
        {
            cook_plan = BuildNativeMaterialCookPlan(document, conversion_settings);
            metrics.requested_texture_bindings = cook_plan.requested_texture_bindings;
            metrics.total_texture_jobs = cook_plan.texture_jobs.size();
        }
        catch (const NativeMaterialConversionError &error)
        {
            Fail(ModelImportErrorCode::ConversionFailed, error.what());
        }

        const std::filesystem::path operation_root =
            CreateOperationRoot(archive_root, source_path.stem().string(),
                                impl_->operation_sequence.fetch_add(1, std::memory_order_relaxed));
        StagingCleanup cleanup{operation_root};
        std::vector<StagedProduct> staged_texture_products;
        std::vector<NativeTextureCookResult> texture_results;
        {
            ReportProgress(request, ModelImportProgressStage::CookingTextures,
                           "cooking material textures", 0, cook_plan.texture_jobs.size());
            MetricTimer timer(metrics, ModelImportMetricStage::TextureCook);
            try
            {
                RunTexturePipeline(document, cook_plan, conversion_settings, request,
                                   execution_policy, operation_root, staged_texture_products,
                                   texture_results, metrics);
            }
            catch (const NativeMaterialConversionError &error)
            {
                Fail(ModelImportErrorCode::ConversionFailed, error.what());
            }
        }

        NativeMaterialConversionResult converted_materials;
        try
        {
            converted_materials = FinalizeNativeMaterials(document, cook_plan,
                                                           std::move(texture_results),
                                                           conversion_settings);
        }
        catch (const NativeMaterialConversionError &error)
        {
            Fail(ModelImportErrorCode::ConversionFailed, error.what());
        }
        converted_materials.metrics.unique_texture_product_count =
            metrics.unique_texture_product_count;
        converted_materials.metrics.texture_product_bytes = metrics.texture_product_bytes;

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
                ValidateNativeModelProductStructure(model_bytes);
                for (const NativeMaterialProduct &material : converted_materials.materials)
                {
                    ValidateNativeMaterialProduct(material.bytes);
                }
                for (const NativeImageProduct &image : converted_materials.embedded_images)
                {
                    try
                    {
                        ValidateNativeTextureProductStructure(image.bytes);
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
        catch (const NativeTextureError &error)
        {
            Fail(ModelImportErrorCode::ProductInvalid, error.what());
        }

        std::vector<StagedProduct> products;
        ContentHash model_hash{};
        {
            MetricTimer timer(metrics, ModelImportMetricStage::ProductHash);
            model_hash = Sha256(model_bytes);
        }
        products.push_back(StageProductBytes(
            operation_root,
            {model_hash, ArchiveProductType::Model,
             ProductRelativePath(ArchiveProductType::Model, model_hash),
             static_cast<std::uint64_t>(model_bytes.size()), request.settings.native_model_version},
            model_bytes, metrics));
        std::vector<std::byte>{}.swap(model_bytes);
        std::vector<SourceProductRecord> source_products;
        source_products.push_back({model_hash, ArchiveProductType::Model, kModelProductRole, -1,
                                   source_path.stem().string()});

        std::vector<ContentHash> material_hashes;
        material_hashes.reserve(converted_materials.materials.size());
        for (std::size_t index = 0; index < converted_materials.materials.size(); ++index)
        {
            NativeMaterialProduct &material = converted_materials.materials[index];
            material_hashes.push_back(material.content_hash);
            products.push_back(StageProductBytes(
                operation_root,
                {material.content_hash, ArchiveProductType::Material,
                 ProductRelativePath(ArchiveProductType::Material, material.content_hash),
                 static_cast<std::uint64_t>(material.bytes.size()),
                 request.settings.material_schema_version},
                material.bytes, metrics));
            std::vector<std::byte>{}.swap(material.bytes);
            const std::string display_name = material.display_name.empty()
                                                  ? "Material_" + std::to_string(index)
                                                  : material.display_name;
            source_products.push_back({material.content_hash, ArchiveProductType::Material,
                                       kMaterialProductRole, static_cast<std::int32_t>(index),
                                       display_name});
        }

        std::vector<ContentHash> texture_hashes;
        texture_hashes.reserve(staged_texture_products.size());
        for (const StagedProduct &product : staged_texture_products)
        {
            texture_hashes.push_back(product.record.content_hash);
        }
        products.insert(products.end(),
                        std::make_move_iterator(staged_texture_products.begin()),
                        std::make_move_iterator(staged_texture_products.end()));

        for (std::size_t index = 0; index < products.size(); ++index)
        {
            StagedProduct &product = products[index];
            ReportProgress(request, ModelImportProgressStage::PublishingProducts,
                           "publishing " + product.record.relative_path,
                           index + 1, products.size());
            MetricTimer timer(metrics, ModelImportMetricStage::Publication);
            PublishProduct(archive_root, product);
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
        for (const StagedProduct &product : products)
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
        result.metrics.texture_prepare_count = converted_materials.metrics.texture_prepare_count;
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
