#include <cstdint>
#include <chrono>
#include <filesystem>
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
#include "asset/texture_importer.h"

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
            << "  import|reimport --source <asset-relative-path> [--importer <id>] [--asset-root <path>] "
               "[--archive-root <path>]\n"
            << "  cook-texture --source <asset-relative-path> [--semantic <generic|color|normal|packed|opacity>] "
               "[--max-dimension <n>] [--asset-root <path>] [--archive-root <path>]\n"
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

    int Run(const CommandLine &command)
    {
        if (command.command == "help" || command.command == "--help")
        {
            PrintUsage();
            return 0;
        }
        const std::filesystem::path asset_root = AssetRoot(command);
        const std::filesystem::path archive_root = ArchiveRoot(command, asset_root);

        if (command.command == "import" || command.command == "reimport")
        {
            kpengine::asset::ModelImportService service{};
            kpengine::asset::ImportProviderRegistry registry{};
            std::string diagnostic;
            const ProgressReporter progress_reporter{};
            if (!kpengine::asset::RegisterModelImportProvider(
                    registry, service, {}, diagnostic,
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
            const auto snapshot = archive.FindSourceByLogicalPath(
                Option(command, "model", true));
            if (!snapshot.has_value())
            {
                std::cout << "model: missing\n";
                return 2;
            }
            PrintSnapshot(*snapshot);
            const auto model_path = archive.ResolveModelProductPath(
                Option(command, "model", true));
            std::cout << "model_path: "
                      << (model_path.has_value() ? model_path->string() : "missing") << '\n';
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
