#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "asset/asset_import_adapters.h"
#include "asset/asset_import_registry.h"
#include "asset/material_promotion.h"
#include "asset/model_archive.h"
#include "asset/model_import_service.h"
#include "asset/native_model.h"
#include "asset/texture_importer.h"

#if defined(KPENGINE_ASSET_TOOL_HAS_LIVE2D)
#include "live2d_import.h"
#endif

namespace
{
    struct CommandLine final
    {
        std::string command;
        std::map<std::string, std::string> options;
    };

    void PrintUsage()
    {
        std::cout
            << "KimPeanutAssetTool\n"
            << "  import|reimport --source <asset-relative-path> [--importer <id>] "
               "[--compression <portable|bc>] [--asset-root <path>] "
               "[--archive-root <path>]\n"
#if defined(KPENGINE_ASSET_TOOL_HAS_LIVE2D)
            << "  import-live2d --source <asset-relative-path> --output <product-path> "
               "[--asset-root <path>] [--archive-root <path>]\n"
#endif
            << "  cook-texture --source <asset-relative-path> [--semantic <generic|color|normal|packed|opacity>] "
               "[--compression <portable|bc>] [--max-dimension <n>] [--asset-root <path>] "
               "[--archive-root <path>]\n"
            << "  status --source <asset-relative-path> [--asset-root <path>] "
               "[--archive-root <path>]\n"
            << "  diagnostics --source <asset-relative-path> [--asset-root <path>] "
               "[--archive-root <path>]\n"
            << "  inspect --model <logical-model-key> [--asset-root <path>] "
               "[--archive-root <path>]\n"
            << "  integrity [--asset-root <path>] [--archive-root <path>]\n"
            << "  promote-material --model <logical-model-key> --slot <n> "
               "--output <asset-relative-material-path> [--asset-root <path>] "
               "[--archive-root <path>]\n";
    }

    CommandLine ParseArguments(int argc, char **argv)
    {
        if (argc < 2)
        {
            PrintUsage();
            throw std::invalid_argument("a command is required");
        }

        CommandLine result{argv[1], {}};
        for (int index = 2; index < argc; ++index)
        {
            const std::string option{argv[index]};
            if (option.rfind("--", 0) != 0 || index + 1 >= argc)
            {
                throw std::invalid_argument("expected an option followed by a value: " + option);
            }
            result.options[option.substr(2)] = argv[++index];
        }
        return result;
    }

    std::string Option(const CommandLine &command, std::string_view name,
                       bool required = false)
    {
        const auto found = command.options.find(std::string{name});
        if (found != command.options.end())
        {
            return found->second;
        }
        if (required)
        {
            throw std::invalid_argument("missing --" + std::string{name});
        }
        return {};
    }

    std::filesystem::path AssetRoot(const CommandLine &command)
    {
        const std::string value = Option(command, "asset-root");
        return value.empty() ? std::filesystem::current_path() / "asset"
                             : std::filesystem::path{value};
    }

    std::vector<std::byte> ReadBytes(const std::filesystem::path &path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            throw std::runtime_error("failed to read file: " + path.string());
        }
        const std::streampos end = stream.tellg();
        if (end < 0)
        {
            throw std::runtime_error("failed to determine file size: " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(end));
        stream.seekg(0, std::ios::beg);
        if (!bytes.empty() &&
            !stream.read(reinterpret_cast<char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size())))
        {
            throw std::runtime_error("failed to read file: " + path.string());
        }
        return bytes;
    }

    std::filesystem::path ArchiveRoot(const CommandLine &command,
                                      const std::filesystem::path &asset_root)
    {
        const std::string value = Option(command, "archive-root");
        return value.empty() ? asset_root / ".archive" : std::filesystem::path{value};
    }

    std::uint32_t Slot(const CommandLine &command)
    {
        const std::string value = Option(command, "slot", true);
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed != value.size() || parsed > UINT32_MAX)
        {
            throw std::invalid_argument("--slot must be a non-negative 32-bit integer");
        }
        return static_cast<std::uint32_t>(parsed);
    }

    kpengine::data::TextureSemantic TextureSemantic(const CommandLine &command)
    {
        const std::string value = Option(command, "semantic");
        if (value.empty() || value == "generic") return kpengine::data::TextureSemantic::Generic;
        if (value == "color") return kpengine::data::TextureSemantic::Color;
        if (value == "normal") return kpengine::data::TextureSemantic::Normal;
        if (value == "packed") return kpengine::data::TextureSemantic::PackedLinear;
        if (value == "opacity") return kpengine::data::TextureSemantic::OpacityMask;
        throw std::invalid_argument("--semantic must be generic, color, normal, packed, or opacity");
    }

    std::uint32_t MaxDimension(const CommandLine &command)
    {
        const std::string value = Option(command, "max-dimension");
        if (value.empty()) return 2048;
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed != value.size() || parsed == 0 || parsed > UINT32_MAX)
        {
            throw std::invalid_argument("--max-dimension must be a positive 32-bit integer");
        }
        return static_cast<std::uint32_t>(parsed);
    }

    kpengine::asset::TextureCompressionPolicy CompressionPolicy(const CommandLine &command)
    {
        const std::string value = Option(command, "compression");
        if (value.empty() || value == "portable")
        {
            return kpengine::asset::TextureCompressionPolicy::Portable;
        }
        if (value == "bc")
        {
            return kpengine::asset::TextureCompressionPolicy::PreferBlockCompression;
        }
        throw std::invalid_argument("--compression must be portable or bc");
    }

    const char *ProgressStageName(kpengine::asset::ModelImportProgressStage stage)
    {
        using Stage = kpengine::asset::ModelImportProgressStage;
        switch (stage)
        {
        case Stage::CheckingCache: return "cache";
        case Stage::DecodingSource: return "decode";
        case Stage::HashingDependencies: return "hash";
        case Stage::CookingTextures: return "cook";
        case Stage::SerializingProducts: return "serialize";
        case Stage::PublishingProducts: return "publish";
        case Stage::UpdatingArchive: return "archive";
        case Stage::Complete: return "done";
        }
        return "unknown";
    }

    class ProgressReporter final
    {
    public:
        ProgressReporter() : started_(std::chrono::steady_clock::now())
        {
        }

        void Report(const kpengine::asset::ModelImportProgress &progress) const
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_);
            std::cout << '[' << std::fixed << std::setprecision(1)
                      << (static_cast<double>(elapsed.count()) / 1000.0) << "s] "
                      << ProgressStageName(progress.stage);
            if (progress.total != 0)
            {
                std::cout << " [" << progress.completed << '/' << progress.total << ']';
            }
            std::cout << ' ' << progress.message << '\n' << std::flush;
        }

    private:
        std::chrono::steady_clock::time_point started_;
    };

    void PrintSnapshot(const kpengine::asset::SourceArchiveSnapshot &snapshot)
    {
        std::cout << "source: " << snapshot.source.normalized_path << '\n'
                  << "status: " << static_cast<int>(snapshot.source.status) << '\n'
                  << "package_hash: " << snapshot.source.package_hash.ToHex() << '\n'
                  << "products: " << snapshot.products.size() << '\n'
                  << "material_overrides: " << snapshot.material_overrides.size() << '\n';
        for (const kpengine::asset::SourceProductRecord &product : snapshot.source_products)
        {
            std::cout << "product: " << product.content_hash.ToHex()
                      << " type=" << static_cast<int>(product.asset_type)
                      << " slot=" << product.slot << '\n';
        }
    }

#if defined(KPENGINE_ASSET_TOOL_HAS_LIVE2D)
    void WriteBytes(const std::filesystem::path &path,
                    const std::vector<std::byte> &bytes)
    {
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            throw std::runtime_error("failed to open file for writing: " + path.string());
        }
        if (!bytes.empty())
        {
            stream.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        if (!stream)
        {
            throw std::runtime_error("failed to write file: " + path.string());
        }
    }

    bool WriteNewProduct(const std::filesystem::path &path,
                         const std::vector<std::byte> &bytes)
    {
        if (std::filesystem::exists(path))
        {
            if (ReadBytes(path) != bytes)
            {
                throw std::runtime_error("immutable product collision: " + path.string());
            }
            return false;
        }

        const auto unique_id = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path temporary = path;
        temporary += ".tmp." + std::to_string(unique_id);
        try
        {
            WriteBytes(temporary, bytes);
            std::error_code error;
            std::filesystem::rename(temporary, path, error);
            if (error)
            {
                throw std::runtime_error("failed to publish product " + path.string() + ": " +
                                         error.message());
            }
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
        return true;
    }

    void RemoveFiles(const std::vector<std::filesystem::path> &paths) noexcept
    {
        for (auto path = paths.rbegin(); path != paths.rend(); ++path)
        {
            std::error_code ignored;
            std::filesystem::remove(*path, ignored);
        }
    }

    std::filesystem::path Live2DArchiveRoot(const CommandLine &command,
                                            const std::filesystem::path &output)
    {
        const auto output_parent =
            std::filesystem::absolute(output.parent_path()).lexically_normal();
        const auto archive = Option(command, "archive-root").empty()
                                 ? output_parent / ".archive"
                                 : std::filesystem::path{Option(command, "archive-root")};
        const auto normalized_archive = std::filesystem::absolute(archive).lexically_normal();
        if (normalized_archive.filename() != ".archive" ||
            normalized_archive.parent_path() != output_parent)
        {
            throw std::invalid_argument(
                "Live2D --archive-root must be the .archive directory beside --output");
        }
        return normalized_archive;
    }

    int RunLive2DImport(const CommandLine &command,
                        const std::filesystem::path &asset_root)
    {
        const std::filesystem::path output = Option(command, "output", true);
        if (output.extension() != ".live2d")
        {
            throw std::invalid_argument("Live2D --output must have a .live2d extension");
        }
        const std::filesystem::path archive_root = Live2DArchiveRoot(command, output);
        const std::filesystem::path stage_root =
            output.parent_path() /
            (".live2d-import-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path stage_archive = stage_root / ".archive";
        std::vector<std::filesystem::path> created_files;
        bool created_product = false;

        try
        {
            kpengine::asset::ImportProviderRegistry registry{};
            std::string diagnostic;
            if (!kpengine::live2d::RegisterLive2DImporters(registry, diagnostic) ||
                !registry.Seal(diagnostic))
            {
                throw std::runtime_error("failed to initialize Live2D import provider: " +
                                         diagnostic);
            }

            kpengine::asset::ImportProviderRequest request{};
            request.asset_root = asset_root;
            request.archive_root = stage_archive;
            request.source_path = Option(command, "source", true);
            const kpengine::asset::ImportProviderResult provider_result =
                registry.Execute(request, "live2d", diagnostic);
            if (provider_result.product == nullptr)
            {
                const std::string message = provider_result.diagnostic.empty()
                                                ? diagnostic
                                                : provider_result.diagnostic;
                throw std::runtime_error("Live2D import failed: " + message);
            }
            const auto result_product =
                std::dynamic_pointer_cast<kpengine::asset::TypedImportProduct<
                    kpengine::live2d::Live2DImportProduct,
                    kpengine::asset::ImportProviderKind::Custom>>(provider_result.product);
            if (result_product == nullptr)
            {
                throw std::runtime_error("Live2D provider returned an invalid result type");
            }

            const auto &product = result_product->value;
            if (std::filesystem::exists(output) && ReadBytes(output) != product.product_bytes)
            {
                throw std::runtime_error("immutable product collision: " + output.string());
            }

            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> staged_files;
            if (std::filesystem::exists(stage_archive))
            {
                for (const auto &entry :
                     std::filesystem::recursive_directory_iterator(stage_archive))
                {
                    if (!entry.is_regular_file()) continue;
                    const auto relative = std::filesystem::relative(entry.path(), stage_archive);
                    staged_files.emplace_back(entry.path(), archive_root / relative);
                    if (std::filesystem::exists(staged_files.back().second) &&
                        ReadBytes(staged_files.back().second) != ReadBytes(entry.path()))
                    {
                        throw std::runtime_error("immutable product collision: " +
                                                 staged_files.back().second.string());
                    }
                }
            }

            for (const auto &[source, destination] : staged_files)
            {
                if (std::filesystem::exists(destination)) continue;
                if (!destination.parent_path().empty())
                {
                    std::filesystem::create_directories(destination.parent_path());
                }
                std::error_code error;
                std::filesystem::copy_file(source, destination,
                                            std::filesystem::copy_options::none, error);
                if (error)
                {
                    throw std::runtime_error("failed to publish texture product " +
                                             destination.string() + ": " + error.message());
                }
                created_files.push_back(destination);
            }

            created_product = WriteNewProduct(output, product.product_bytes);
            std::error_code ignored;
            std::filesystem::remove_all(stage_root, ignored);
            std::cout << "Imported\n"
                      << "source: " << request.source_path.string() << '\n'
                      << "product: " << output.string() << '\n'
                      << "archive: " << archive_root.string() << '\n'
                      << "textures: " << product.product.textures.size() << '\n';
            return 0;
        }
        catch (...)
        {
            RemoveFiles(created_files);
            if (created_product)
            {
                std::error_code ignored;
                std::filesystem::remove(output, ignored);
            }
            std::error_code ignored;
            std::filesystem::remove_all(stage_root, ignored);
            throw;
        }
    }
#endif

    int Run(const CommandLine &command)
    {
        if (command.command == "help" || command.command == "--help")
        {
            PrintUsage();
            return 0;
        }
        const std::filesystem::path asset_root = AssetRoot(command);
        const std::filesystem::path archive_root = ArchiveRoot(command, asset_root);

#if defined(KPENGINE_ASSET_TOOL_HAS_LIVE2D)
        if (command.command == "import-live2d")
        {
            return RunLive2DImport(command, asset_root);
        }
#endif

        if (command.command == "import" || command.command == "reimport")
        {
            kpengine::asset::ModelImportService service{};
            kpengine::asset::ImportProviderRegistry registry{};
            std::string diagnostic;
            kpengine::asset::ModelImportSettings model_settings{};
            model_settings.texture_settings.compression = CompressionPolicy(command);
            const ProgressReporter progress_reporter{};
            if (!kpengine::asset::RegisterModelImportProvider(
                    registry, service, model_settings, diagnostic,
                    [&progress_reporter](const kpengine::asset::ModelImportProgress &progress)
                    {
                        progress_reporter.Report(progress);
                    }) ||
                !registry.Seal(diagnostic))
            {
                throw std::runtime_error("failed to initialize model import providers: " + diagnostic);
            }
            kpengine::asset::ImportProviderRequest request{};
            request.asset_root = asset_root;
            request.archive_root = archive_root;
            request.source_path = Option(command, "source", true);
            const kpengine::asset::ImportProviderResult provider_result =
                registry.Execute(request, Option(command, "importer"), diagnostic);
            if (provider_result.product == nullptr)
            {
                throw std::runtime_error("model import provider failed: " + diagnostic);
            }
            const auto result_product =
                std::dynamic_pointer_cast<kpengine::asset::TypedImportProduct<
                    kpengine::asset::ModelImportResult,
                    kpengine::asset::ImportProviderKind::Model>>(provider_result.product);
            if (result_product == nullptr)
            {
                throw std::runtime_error("model import provider returned an invalid result type");
            }
            const kpengine::asset::ModelImportResult &result = result_product->value;
            std::cout << (result.status == kpengine::asset::ModelImportStatus::UpToDate
                              ? "UpToDate"
                              : "Imported")
                      << '\n'
                      << "source: " << result.normalized_source_path << '\n'
                      << "model: " << result.model_path.string() << '\n'
                      << "model_hash: " << result.model_hash.ToHex() << '\n'
                      << "materials: " << result.material_hashes.size() << '\n'
                      << "textures: " << result.texture_hashes.size() << '\n';
            return 0;
        }

        if (command.command == "cook-texture")
        {
            kpengine::asset::ImportProviderRegistry registry{};
            std::string diagnostic;
            kpengine::asset::TextureCookSettings settings{};
            settings.semantic = TextureSemantic(command);
            settings.max_dimension = MaxDimension(command);
            settings.compression = CompressionPolicy(command);
            if (!kpengine::asset::RegisterTextureImportProvider(registry, settings, diagnostic) ||
                !registry.Seal(diagnostic))
            {
                throw std::runtime_error("failed to initialize texture import providers: " + diagnostic);
            }
            const std::filesystem::path source = asset_root / Option(command, "source", true);
            const ProgressReporter progress_reporter{};
            progress_reporter.Report({kpengine::asset::ModelImportProgressStage::DecodingSource,
                                       "decoding " + source.string(), 0, 0});
            kpengine::asset::ImportProviderRequest request{};
            request.asset_root = asset_root;
            request.archive_root = archive_root;
            request.source_path = Option(command, "source", true);
            const kpengine::asset::ImportProviderResult provider_result =
                registry.Execute(request, Option(command, "importer"), diagnostic);
            const auto result_product =
                std::dynamic_pointer_cast<kpengine::asset::TypedImportProduct<
                    kpengine::asset::CookedTexture,
                    kpengine::asset::ImportProviderKind::Texture>>(provider_result.product);
            if (result_product == nullptr)
            {
                throw std::runtime_error("texture import provider failed: " + diagnostic);
            }
            const kpengine::asset::CookedTexture &cooked = result_product->value;
            progress_reporter.Report({kpengine::asset::ModelImportProgressStage::PublishingProducts,
                                       "publishing native texture product", 1, 1});
            const auto product_path = archive_root / kpengine::asset::ProductRelativePath(
                kpengine::asset::ArchiveProductType::Texture, cooked.product_hash, "texture");
            std::cout << "Cooked\n"
                      << "source: " << source.string() << '\n'
                      << "product: " << product_path.string() << '\n'
                      << "hash: " << cooked.product_hash.ToHex() << '\n'
                      << "extent: " << cooked.data.width << 'x' << cooked.data.height << '\n'
                      << "mips: " << cooked.data.GetMipLevelCount() << '\n';
            return 0;
        }

        if (command.command == "promote-material")
        {
            kpengine::asset::MaterialPromotionRequest request{};
            request.asset_root = asset_root;
            request.archive_root = archive_root;
            request.logical_model_path = Option(command, "model", true);
            request.material_slot = Slot(command);
            request.authored_material_path = Option(command, "output", true);
            const auto result = kpengine::asset::PromoteGeneratedMaterial(request);
            std::cout << "promoted: " << result.authored_material_path.string() << '\n'
                      << "generated_hash: " << result.generated_material_hash.ToHex() << '\n'
                      << "created: " << (result.authored_file_created ? "true" : "false") << '\n';
            return 0;
        }

        kpengine::asset::ModelArchiveDatabase archive{archive_root / "archive.sqlite3"};
        if (command.command == "integrity")
        {
            archive.IntegrityCheck();
            std::cout << "archive: OK\n";
            return 0;
        }

        if (command.command == "status" || command.command == "diagnostics")
        {
            const std::string source =
                kpengine::asset::NormalizeAssetRelativePath(Option(command, "source", true));
            const auto snapshot = archive.FindSource(source);
            if (!snapshot.has_value())
            {
                std::cout << "source: missing\n";
                return 2;
            }
            PrintSnapshot(*snapshot);
            if (command.command == "diagnostics")
            {
                archive.IntegrityCheck();
                std::cout << "archive: OK\n";
            }
            return 0;
        }

        if (command.command == "inspect")
        {
            const std::string logical_model_path = Option(command, "model", true);
            const auto snapshot = archive.FindSourceByLogicalPath(logical_model_path);
            if (!snapshot.has_value())
            {
                std::cout << "model: missing\n";
                return 2;
            }
            PrintSnapshot(*snapshot);
            if (snapshot->source.status != kpengine::asset::SourceImportStatus::Ready)
            {
                throw kpengine::asset::ModelArchiveError(
                    kpengine::asset::ModelArchiveErrorCode::SourceNotFound,
                    "logical model source is not ready: " + logical_model_path);
            }

            const kpengine::asset::ProductRecord *model_record = nullptr;
            for (const auto &source_product : snapshot->source_products)
            {
                if (source_product.asset_type != kpengine::asset::ArchiveProductType::Model)
                {
                    continue;
                }
                for (const auto &product : snapshot->products)
                {
                    if (product.asset_type == kpengine::asset::ArchiveProductType::Model &&
                        product.content_hash == source_product.content_hash)
                    {
                        model_record = &product;
                        break;
                    }
                }
                if (model_record != nullptr)
                {
                    break;
                }
            }
            if (model_record == nullptr)
            {
                throw kpengine::asset::ModelArchiveError(
                    kpengine::asset::ModelArchiveErrorCode::InvalidDatabase,
                    "logical model source has no Model product");
            }

            const std::filesystem::path model_path = archive.ArchiveRoot() /
                                                      model_record->relative_path;
            std::cout << "model_path: " << model_path.string() << '\n';

            const auto read_started = std::chrono::steady_clock::now();
            const std::vector<std::byte> model_bytes = ReadBytes(model_path);
            const auto read_finished = std::chrono::steady_clock::now();
            const auto decode_started = read_finished;
            const kpengine::asset::NativeModelProduct product =
                kpengine::asset::DeserializeNativeModel(model_bytes);
            const auto decode_finished = std::chrono::steady_clock::now();
            std::string product_diagnostic;
            if (model_bytes.size() != model_record->byte_size ||
                product.product_hash != model_record->content_hash ||
                !kpengine::asset::VerifyArchiveProduct(
                    model_path, kpengine::asset::ArchiveProductType::Model, model_bytes,
                    product_diagnostic, archive.ArchiveRoot(), product.product_hash))
            {
                throw kpengine::asset::ModelArchiveError(
                    kpengine::asset::ModelArchiveErrorCode::CorruptProduct,
                    product_diagnostic.empty()
                        ? "archive Model product failed integrity verification: " +
                              model_path.string()
                        : product_diagnostic);
            }
            const auto read_seconds = std::chrono::duration<double>(read_finished - read_started);
            const auto decode_seconds = std::chrono::duration<double>(decode_finished - decode_started);
            std::cout << std::fixed << std::setprecision(6)
                      << "format_version: " << product.format_version << '\n'
                      << "format_features: " << product.format_features << '\n'
                      << "product_bytes: " << product.product_bytes << '\n'
                      << "decoded_payload_bytes: " << product.decoded_payload_bytes << '\n'
                      << "vertex_count: " << product.data.vertices.size() << '\n'
                      << "index_count: " << product.data.indices.size() << '\n'
                      << "section_count: " << product.data.sections.size() << '\n'
                      << "material_count: " << product.data.material_references.size() << '\n'
                      << "vertex_stride: " << product.vertex_stride << '\n'
                      << "index_stride: " << product.index_stride << '\n'
                      << "read_seconds: " << read_seconds.count() << '\n'
                      << "decode_seconds: " << decode_seconds.count() << '\n';
            return 0;
        }

        PrintUsage();
        throw std::invalid_argument("unknown command: " + command.command);
    }
}

int main(int argc, char **argv)
{
    try
    {
        return Run(ParseArguments(argc, argv));
    }
    catch (const kpengine::asset::ModelImportError &error)
    {
        std::cerr << "import error (" << static_cast<int>(error.Code()) << "): "
                  << error.what() << '\n';
    }
    catch (const kpengine::asset::MaterialPromotionError &error)
    {
        std::cerr << "promotion error (" << static_cast<int>(error.Code()) << "): "
                  << error.what() << '\n';
    }
    catch (const kpengine::asset::ModelArchiveError &error)
    {
        std::cerr << "archive error (" << static_cast<int>(error.Code()) << "): "
                  << error.what() << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
    }
    return 1;
}
