#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "asset/material_promotion.h"
#include "asset/model_archive.h"
#include "asset/model_import_service.h"

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
            << "  import|reimport --source <asset-relative-path> [--asset-root <path>] "
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
            kpengine::asset::ModelImportRequest request{};
            request.asset_root = asset_root;
            request.archive_root = archive_root;
            request.source_path = Option(command, "source", true);
            const kpengine::asset::ModelImportResult result =
                kpengine::asset::ImportModel(request);
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
