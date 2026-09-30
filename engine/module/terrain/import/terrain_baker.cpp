#include "terrain_baker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

#include "asset/model_archive.h"
#include "asset/native_material.h"
#include "asset/native_model.h"
#include "config/path.h"
#include "terrain_material_settings.h"
#include "product/terrain_core.h"

namespace kpengine::terrain
{
    namespace
    {
        using namespace asset;

        class StagingDirectoryCleanup final
        {
        public:
            explicit StagingDirectoryCleanup(std::filesystem::path path)
                : path_(std::move(path)) {}
            ~StagingDirectoryCleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
            }
            StagingDirectoryCleanup(const StagingDirectoryCleanup &) = delete;
            StagingDirectoryCleanup &operator=(const StagingDirectoryCleanup &) = delete;

        private:
            std::filesystem::path path_;
        };

        void WriteBytes(const std::filesystem::path &path,
                        const std::vector<std::byte> &bytes)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) throw std::runtime_error("could not create staging directory: " + error.message());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("could not open staged product: " + path.string());
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("could not write staged product: " + path.string());
        }

        std::vector<std::byte> ReadBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("could not read product: " + path.string());
            const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
            std::vector<std::byte> bytes(chars.size());
            std::transform(chars.begin(), chars.end(), bytes.begin(),
                [](char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
            return bytes;
        }

        std::string ReadTextFile(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("could not read settings file: " + path.string());
            return {std::istreambuf_iterator<char>(input), {}};
        }

        void Publish(const std::filesystem::path &archive_root,
                     const ProductRecord &record,
                     const std::filesystem::path &staged_path,
                     const std::vector<std::byte> &bytes)
        {
            const std::filesystem::path destination = archive_root / record.relative_path;
            std::error_code error;
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error) throw std::runtime_error("could not create product directory: " + error.message());
            if (std::filesystem::exists(destination, error) && !error)
            {
                std::string diagnostic;
                if (!VerifyArchiveProduct(destination, record.asset_type, bytes, diagnostic, archive_root))
                    throw std::runtime_error("content-addressed product collision: " + diagnostic);
                return;
            }
            if (error) throw std::runtime_error("could not inspect product destination: " + error.message());
            std::filesystem::create_hard_link(staged_path, destination, error);
            if (error)
            {
                std::error_code inspect_error;
                if (std::filesystem::exists(destination, inspect_error) && !inspect_error)
                {
                    std::string diagnostic;
                    if (VerifyArchiveProduct(destination, record.asset_type, bytes,
                                             diagnostic, archive_root)) return;
                }
                throw std::runtime_error("could not atomically publish product: " + error.message());
            }
        }
    }

    TerrainBakeResult TerrainBaker::Bake(const TerrainRecipe &recipe,
                                         const ScalarField2D &heightfield)
    {
        using namespace asset;
        TerrainBakeResult result;
        const auto bake_started = std::chrono::steady_clock::now();
        const std::filesystem::path content_root{GetContentDirectory()};
        // Terrain products are visible through the content catalogue, which
        // reads its model archive from content/.archive.
        const std::filesystem::path archive_root{GetContentArchiveDirectory()};
        const std::string source_path = "model/terrain/generated/default.kpt";
        result.logical_model_path = "model/terrain/generated/default";
        try
        {
            if (!(heightfield.Domain() == recipe.domain))
                throw std::invalid_argument("bake field domain does not match its recipe");

            data::MeshData mesh = BuildHeightfieldMesh(heightfield);
            if (mesh.vertices.empty() || mesh.indices.empty())
                throw std::invalid_argument("cannot bake an empty heightfield mesh");
            TerrainMaterialSettings terrain_materials;
            const std::filesystem::path settings_path =
                std::filesystem::path{GetAssetDirectory()} /
                    "terrain/material/mountain_basin.terrainmaterial.json";
            std::string settings_diagnostic;
            if (!LoadTerrainMaterialSettings(settings_path, content_root.parent_path() / "asset",
                                             terrain_materials, settings_diagnostic))
                throw std::runtime_error("could not load Terrain material settings: " + settings_diagnostic);
            AssignTerrainMaterialSections(mesh);
            const float max_float = std::numeric_limits<float>::max();
            spatial::AABB bounds{{max_float, max_float, max_float},
                                 {-max_float, -max_float, -max_float}};
            for (const data::Vertex &vertex : mesh.vertices) bounds.ExpandToInclude(vertex.position);
            for (data::MeshSection &section : mesh.sections)
            {
                section.local_bounds = {{max_float, max_float, max_float},
                                        {-max_float, -max_float, -max_float}};
                for (std::uint32_t index = section.index_start;
                     index < section.index_start + section.index_count; ++index)
                    section.local_bounds.ExpandToInclude(mesh.vertices[mesh.indices[index]].position);
            }
            NativeMaterialConversionSettings material_settings;
            material_settings.asset_root = GetAssetDirectory();
            material_settings.archive_root = archive_root;
            ImportedModelDocument composite_document;
            BuildTerrainLayerMaterial(terrain_materials, heightfield, composite_document);
            NativeMaterialConversionResult materials =
                ConvertImportedMaterials(composite_document, material_settings);
            if (materials.materials.size() != 1)
                throw std::runtime_error("Terrain layer material preparation returned an unexpected material count");

            NativeModelData model;
            model.vertices = std::move(mesh.vertices);
            model.indices = std::move(mesh.indices);
            model.sections = std::move(mesh.sections);
            model.local_bounds = bounds;
            for (const NativeMaterialProduct &material : materials.materials)
                model.material_references.push_back({AssetType::KPAT_Material,
                                                      material.content_hash});
            const std::vector<std::byte> model_bytes = SerializeNativeModel(model);
            result.model_bytes = model_bytes.size();
            for (const NativeMaterialProduct &material : materials.materials)
                result.material_bytes += material.bytes.size();
            ValidateNativeModelProductStructure(model_bytes);
            for (const NativeMaterialProduct &material : materials.materials)
                ValidateNativeMaterialProduct(material.bytes);
            const ContentHash model_hash = Sha256(model_bytes);

            const nlohmann::json recipe_json = recipe.ToJson();
            const std::string recipe_text = recipe_json.dump(2) + "\n";
            const std::vector<std::byte> recipe_bytes = [&recipe_text]
            {
                std::vector<std::byte> bytes(recipe_text.size());
                std::transform(recipe_text.begin(), recipe_text.end(), bytes.begin(),
                    [](char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
                return bytes;
            }();
            const ContentHash recipe_hash = Sha256(recipe_bytes);
            const std::string provenance_relative =
                "terrain/generated/recipes/" + recipe_hash.ToHex() + ".kpterrain";
            result.provenance_path =
                (content_root / provenance_relative).generic_string();
            const std::filesystem::path provenance_file = content_root / provenance_relative;
            std::error_code error;
            std::filesystem::create_directories(provenance_file.parent_path(), error);
            if (error) throw std::runtime_error("could not create Terrain provenance folder: " + error.message());
            const bool provenance_exists = std::filesystem::exists(provenance_file, error);
            if (error) throw std::runtime_error("could not inspect recipe provenance: " + error.message());
            if (provenance_exists)
            {
                if (!std::filesystem::is_regular_file(provenance_file, error) || error ||
                    std::filesystem::file_size(provenance_file, error) != recipe_bytes.size() || error ||
                    Sha256File(provenance_file) != recipe_hash)
                    throw std::runtime_error("recipe provenance hash path contains different data");
            }
            else
            {
                const std::filesystem::path temporary = provenance_file.string() + ".tmp";
                std::vector<std::byte> provenance_bytes = recipe_bytes;
                WriteBytes(temporary, provenance_bytes);
                std::filesystem::rename(temporary, provenance_file, error);
                if (error && !std::filesystem::exists(provenance_file))
                    throw std::runtime_error("could not publish recipe provenance: " + error.message());
            }

            const std::string operation = model_hash.ToHex() + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            const std::filesystem::path staging = archive_root / "staging" / ("terrain-" + operation);
            std::filesystem::create_directories(staging, error);
            if (error) throw std::runtime_error("could not create Terrain bake staging folder: " + error.message());
            StagingDirectoryCleanup staging_cleanup{staging};
            const ProductRecord model_record{model_hash, ArchiveProductType::Model,
                ProductRelativePath(ArchiveProductType::Model, model_hash),
                model_bytes.size(), kNativeModelVersion};
            const auto model_staged = staging / model_record.relative_path;
            WriteBytes(model_staged, model_bytes);
            Publish(archive_root, model_record, model_staged, model_bytes);
            std::vector<ProductRecord> products{model_record};
            std::vector<SourceProductRecord> source_products{
                {model_hash, ArchiveProductType::Model, 0, -1, "Terrain Default"}};
            for (std::size_t index = 0; index < materials.materials.size(); ++index)
            {
                const NativeMaterialProduct &material = materials.materials[index];
                const ProductRecord record{material.content_hash, ArchiveProductType::Material,
                    ProductRelativePath(ArchiveProductType::Material, material.content_hash),
                    material.bytes.size(), kNativeMaterialSchemaVersion};
                const std::filesystem::path staged_path = staging / record.relative_path;
                WriteBytes(staged_path, material.bytes);
                Publish(archive_root, record, staged_path, material.bytes);
                products.push_back(record);
                source_products.push_back({material.content_hash, ArchiveProductType::Material,
                    1, static_cast<std::int32_t>(index), material.display_name});
            }
            for (const NativeImageProduct &texture : materials.embedded_images)
            {
                const ProductRecord record{texture.content_hash, ArchiveProductType::Texture,
                    ProductRelativePath(ArchiveProductType::Texture, texture.content_hash, "texture"),
                    texture.bytes.size(), 1};
                const std::filesystem::path staged_path = staging / record.relative_path;
                WriteBytes(staged_path, texture.bytes);
                Publish(archive_root, record, staged_path, texture.bytes);
                products.push_back(record);
                source_products.push_back({texture.content_hash, ArchiveProductType::Texture,
                    2, -1, "Terrain Texture"});
            }

            const std::filesystem::file_time_type write_time =
                std::filesystem::last_write_time(provenance_file, error);
            if (error) throw std::runtime_error("could not inspect recipe provenance time: " + error.message());
            const auto write_key = write_time.time_since_epoch().count();
            std::vector<SourceDependencyRecord> dependencies{{provenance_relative, recipe_hash,
                recipe_bytes.size(), static_cast<std::int64_t>(write_key)}};
            const auto add_dependency = [&](const std::filesystem::path &path)
            {
                const std::string normalized = std::filesystem::relative(path, content_root.parent_path())
                                                   .generic_string();
                const auto timestamp = std::filesystem::last_write_time(path, error);
                if (error) throw std::runtime_error("could not inspect Terrain material input time: " + error.message());
                dependencies.push_back({normalized, Sha256File(path), std::filesystem::file_size(path),
                    static_cast<std::int64_t>(timestamp.time_since_epoch().count())});
            };
            add_dependency(settings_path);
            for (const std::filesystem::path &path : terrain_materials.source_files) add_dependency(path);
            SourceRecord source;
            source.normalized_path = source_path;
            source.path_hash = Sha256(source_path);
            source.display_name = "Terrain Default";
            std::vector<SourceFingerprintInput> fingerprint_inputs;
            fingerprint_inputs.reserve(dependencies.size());
            for (const SourceDependencyRecord &dependency : dependencies)
                fingerprint_inputs.push_back({dependency.normalized_path, dependency.content_hash});
            source.package_hash = HashSourcePackage(fingerprint_inputs);
            source.importer_id = "kpengine.terrain.heightfield";
            source.importer_version = 1;
            source.settings_hash = Sha256(recipe_text + ReadTextFile(settings_path));
            source.native_model_version = kNativeModelVersion;
            source.status = SourceImportStatus::Ready;
            ModelArchiveDatabase archive{archive_root / "archive.sqlite3"};
            archive.ReplaceSource(source, dependencies, products, source_products, {});
            result.succeeded = true;
        }
        catch (const std::exception &error)
        {
            result.diagnostic = error.what();
        }
        result.bake_time_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - bake_started).count();
        return result;
    }
}
