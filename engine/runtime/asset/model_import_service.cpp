#include "model_import_service.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

#include "assimp_model_decoder.h"
#include "image_io/image_io.h"
#include "native_material.h"
#include "native_model.h"

namespace kpengine::asset
{
    namespace
    {
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

        std::vector<std::byte> ReadBytes(const std::filesystem::path &path)
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

        void WriteBytes(const std::filesystem::path &path, const std::vector<std::byte> &bytes)
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
            append_string("KPENGINE_IMPORT_SETTINGS_V1");
            append_string(settings.importer_id);
            append_u32(settings.importer_version);
            append_u32(settings.native_model_version);
            append_u32(settings.material_schema_version);
            append_string(settings.shader_asset_path);
            return bytes;
        }

        ContentHash SettingsHash(const ModelImportSettings &settings)
        {
            if (settings.importer_id.empty() || settings.importer_version == 0 ||
                settings.native_model_version != kNativeModelVersion ||
                settings.material_schema_version != kNativeMaterialSchemaVersion ||
                settings.shader_asset_path.empty())
            {
                Fail(ModelImportErrorCode::InvalidArgument, "model import settings are incomplete");
            }
            return Sha256(SettingsBytes(settings));
        }

        std::vector<SourceDependencyRecord> HashDependencies(
            const ImportedModelDocument &document, const std::filesystem::path &asset_root,
            const std::filesystem::path &source_path, const std::string &source_relative_path)
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
            const SourceArchiveSnapshot &snapshot, const std::filesystem::path &asset_root)
        {
            std::vector<SourceDependencyRecord> result;
            result.reserve(snapshot.dependencies.size());
            for (const SourceDependencyRecord &dependency : snapshot.dependencies)
            {
                const std::string normalized = NormalizeAssetRelativePath(dependency.normalized_path);
                const ContentHash current = Sha256File(asset_root / normalized);
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
                              const SourceArchiveSnapshot &snapshot)
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
                DeserializeNativeModel(ReadBytes(ProductPath(archive_root, *model_product)));
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
                const std::vector<std::byte> bytes = ReadBytes(ProductPath(archive_root, product));
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
                    const image_io::ImageDecodeResult decoded = image_io::DecodeImageMemory(bytes);
                    if (!decoded.result.success)
                    {
                        Fail(ModelImportErrorCode::ProductInvalid,
                             "archive texture product cannot be decoded: " + product.relative_path);
                    }
                }
            }
        }

        std::optional<ModelImportResult> TryCacheHit(
            const ModelImportRequest &request, const std::filesystem::path &asset_root,
            const std::filesystem::path &archive_root, const std::string &source_relative_path,
            const ContentHash &settings_hash, std::int32_t busy_timeout_ms)
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
                dependencies = HashRecordedDependencies(*existing, asset_root);
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
                ValidateProducts(archive_root, *probe.snapshot);
            }
            catch (const ModelImportError &)
            {
                return std::nullopt;
            }

            ModelImportResult result;
            result.status = ModelImportStatus::UpToDate;
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
                            const PendingProduct &product)
        {
            if (Sha256(product.bytes) != product.record.content_hash)
            {
                Fail(ModelImportErrorCode::ProductInvalid,
                     "staged product bytes do not match their content hash");
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
            WriteBytes(staged, product.bytes);
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

        std::error_code error;
        std::filesystem::create_directories(archive_root, error);
        if (error)
        {
            Fail(ModelImportErrorCode::PublicationFailed,
                 "failed to create archive root: " + error.message());
        }

        try
        {
            if (const std::optional<ModelImportResult> hit =
                    TryCacheHit(request, asset_root, archive_root, source_relative_path,
                                settings_hash, impl_->busy_timeout_ms))
            {
                return *hit;
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
            AssimpModelDecoder decoder;
            document = decoder.Decode(source_path);
        }
        catch (const ImportedModelDecodeError &error)
        {
            Fail(ModelImportErrorCode::DecodeFailed, error.what());
        }

        std::vector<SourceDependencyRecord> dependencies;
        try
        {
            dependencies = HashDependencies(document, asset_root, source_path, source_relative_path);
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
            converted_materials = ConvertImportedMaterials(
                document, {asset_root, request.settings.shader_asset_path});
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
            model_bytes = SerializeNativeModel(model_data);
            const NativeModelProduct decoded = DeserializeNativeModel(model_bytes);
            if (!(decoded.data == model_data))
            {
                Fail(ModelImportErrorCode::ProductInvalid,
                     "serialized native model failed its round-trip validation");
            }
            for (const NativeMaterialProduct &material : converted_materials.materials)
            {
                ValidateNativeMaterialProduct(material.bytes);
            }
            for (const NativeImageProduct &image : converted_materials.embedded_images)
            {
                const image_io::ImageDecodeResult decoded_image = image_io::DecodeImageMemory(image.bytes);
                if (!decoded_image.result.success)
                {
                    Fail(ModelImportErrorCode::ProductInvalid,
                         "serialized embedded image failed validation");
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
        const ContentHash model_hash = Sha256(model_bytes);
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
                                                      image.content_hash, image.extension),
                                 static_cast<std::uint64_t>(image.bytes.size()), 1},
                                image.bytes});
        }

        const std::filesystem::path operation_root =
            archive_root / "staging" /
            (source_path.stem().string() + "-" + std::to_string(
                impl_->operation_sequence.fetch_add(1, std::memory_order_relaxed)));
        StagingCleanup cleanup{operation_root};
        for (const PendingProduct &product : products)
        {
            WriteBytes(operation_root / product.record.relative_path, product.bytes);
        }
        for (const PendingProduct &product : products)
        {
            PublishProduct(archive_root, operation_root, product);
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
        return result;
    }

    ModelImportResult ImportModel(const ModelImportRequest &request)
    {
        return ModelImportService{}.Import(request);
    }
}
