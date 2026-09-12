#include "live2d_import.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <string_view>

#include <nlohmann/json.hpp>

#include "asset/texture_importer.h"

namespace kpengine::live2d
{
    namespace
    {
        using Json = nlohmann::json;

        std::string Lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        bool IsWithin(const std::filesystem::path &root,
                      const std::filesystem::path &candidate)
        {
            const std::string root_text = Lowercase(root.lexically_normal().generic_string());
            const std::string candidate_text =
                Lowercase(candidate.lexically_normal().generic_string());
            return candidate_text == root_text ||
                   (candidate_text.size() > root_text.size() &&
                    candidate_text.rfind(root_text + "/", 0) == 0);
        }

        bool ResolveSourcePath(const std::filesystem::path &root,
                               const std::filesystem::path &base,
                               const std::string &authored,
                               std::filesystem::path &resolved,
                               std::string &relative,
                               std::string &diagnostic)
        {
            if (authored.empty() || authored.find('\0') != std::string::npos)
            {
                diagnostic = "Live2D source reference is empty or contains NUL";
                return false;
            }
            const std::filesystem::path reference{authored};
            if (reference.is_absolute() || reference.has_root_name() ||
                reference.has_root_directory())
            {
                diagnostic = "Live2D source reference must be relative";
                return false;
            }
            resolved = (base / reference).lexically_normal();
            if (!IsWithin(root, resolved))
            {
                diagnostic = "Live2D source reference escapes the asset root";
                return false;
            }
            const std::filesystem::path relative_path = resolved.lexically_relative(root);
            relative = relative_path.generic_string();
            return !relative.empty();
        }

        bool ReadBytes(const std::filesystem::path &path,
                       std::vector<std::byte> &bytes,
                       std::string &diagnostic)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > 512ull * 1024ull * 1024ull)
            {
                diagnostic = "Live2D source file is missing or too large: " +
                             path.generic_string();
                return false;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                diagnostic = "failed to open Live2D source file: " + path.generic_string();
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            file.read(reinterpret_cast<char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                diagnostic = "failed to read Live2D source file: " + path.generic_string();
                return false;
            }
            return true;
        }

        bool ReadJson(const std::filesystem::path &path, Json &json,
                      std::string &diagnostic)
        {
            std::ifstream file(path);
            if (!file)
            {
                diagnostic = "failed to open Live2D JSON source: " + path.generic_string();
                return false;
            }
            try
            {
                file >> json;
            }
            catch (const std::exception &error)
            {
                diagnostic = "malformed Live2D JSON source: " + std::string(error.what());
                return false;
            }
            return true;
        }

        bool AddReference(const std::filesystem::path &root,
                          const std::filesystem::path &source_directory,
                          const std::string &role,
                          const std::string &authored,
                          Live2DProductData &resource,
                          std::string &diagnostic)
        {
            std::filesystem::path resolved;
            std::string relative;
            if (!ResolveSourcePath(root, source_directory, authored, resolved, relative,
                                   diagnostic))
            {
                return false;
            }
            std::vector<std::byte> bytes;
            if (!ReadBytes(resolved, bytes, diagnostic))
            {
                return false;
            }
            resource.optional_chunks.push_back({role, std::move(bytes)});
            return true;
        }

        // A leaf entry under FileReferences -- a motion or an expression -- names
        // its file under "File" and carries its remaining members as metadata.
        // The expression member "Name" is a display name, so walking every string
        // of the object resolved it as a path and made a model that ships
        // expressions fail to import.
        bool IsFileEntry(const Json &value)
        {
            return value.is_object() && value.contains("File") &&
                   value["File"].is_string();
        }

        bool AddReferenceValue(const std::filesystem::path &root,
                               const std::filesystem::path &source_directory,
                               const std::string &role,
                               const Json &value,
                               Live2DProductData &resource,
                               std::string &diagnostic)
        {
            if (value.is_string())
            {
                return AddReference(root, source_directory, role, value.get<std::string>(),
                                    resource, diagnostic);
            }
            if (value.is_array())
            {
                for (std::size_t index = 0; index < value.size(); ++index)
                {
                    if (!AddReferenceValue(root, source_directory,
                                           role + "/" + std::to_string(index), value[index],
                                           resource, diagnostic))
                    {
                        return false;
                    }
                }
            }
            else if (value.is_object())
            {
                // Inside a leaf entry only "File" and "Sound" are references;
                // the remaining members are metadata and are not paths. Above a
                // leaf entry every member is either a reference or a container
                // of references, so the walk stays generic.
                const bool file_entry = IsFileEntry(value);
                for (const auto &[key, child] : value.items())
                {
                    if (file_entry && key != "File" && key != "Sound")
                    {
                        continue;
                    }
                    if (!AddReferenceValue(root, source_directory, role + "/" + key, child,
                                           resource, diagnostic))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool BuildResource(const asset::ImportProviderRequest &request,
                           Live2DProductData &resource,
                           std::string &diagnostic)
        {
            constexpr std::string_view kModel3Suffix = ".model3.json";
            const std::filesystem::path root =
                std::filesystem::absolute(request.asset_root).lexically_normal();
            const std::filesystem::path source =
                request.source_path.is_absolute()
                    ? request.source_path.lexically_normal()
                    : (root / request.source_path).lexically_normal();
            if (!IsWithin(root, source))
            {
                diagnostic = "Live2D model3 source escapes the asset root";
                return false;
            }
            const std::string source_filename =
                Lowercase(source.filename().generic_string());
            if (source_filename.size() < kModel3Suffix.size() ||
                source_filename.compare(source_filename.size() - kModel3Suffix.size(),
                                        kModel3Suffix.size(), kModel3Suffix) != 0)
            {
                diagnostic = "Live2D importer requires a .model3.json source";
                return false;
            }

            Json document;
            if (!ReadJson(source, document, diagnostic) || !document.is_object() ||
                !document.contains("Version") || !document["Version"].is_number_unsigned() ||
                document["Version"].get<std::uint32_t>() != 3 ||
                !document.contains("FileReferences") || !document["FileReferences"].is_object())
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D model3 source has an invalid Version or FileReferences";
                }
                return false;
            }
            resource.model3_version = 3;
            const Json &references = document["FileReferences"];
            if (!references.contains("Moc") || !references["Moc"].is_string() ||
                !references.contains("Textures") || !references["Textures"].is_array() ||
                references["Textures"].empty())
            {
                diagnostic = "Live2D model3 source lacks Moc or ordered Textures";
                return false;
            }

            std::filesystem::path resolved_moc;
            std::string relative_moc;
            if (!ResolveSourcePath(root, source.parent_path(), references["Moc"].get<std::string>(),
                                   resolved_moc, relative_moc, diagnostic) ||
                Lowercase(resolved_moc.extension().generic_string()) != ".moc3" ||
                !ReadBytes(resolved_moc, resource.moc_bytes, diagnostic))
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D model3 source has an invalid Moc reference";
                }
                return false;
            }

            std::set<std::string> texture_paths;
            std::vector<asset::CookedTexture> cooked_textures;
            if (request.archive_root.empty())
            {
                diagnostic = "Live2D import requires an archive root for native textures";
                return false;
            }
            if (request.archive_root.filename().generic_string() != ".archive")
            {
                diagnostic = "Live2D native textures require an archive root named .archive";
                return false;
            }

            for (const Json &texture : references["Textures"])
            {
                if (!texture.is_string())
                {
                    diagnostic = "Live2D model3 Textures contains a non-string entry";
                    return false;
                }
                std::filesystem::path resolved;
                std::string relative;
                if (!ResolveSourcePath(root, source.parent_path(), texture.get<std::string>(),
                                       resolved, relative, diagnostic))
                {
                    return false;
                }
                if (!texture_paths.insert(relative).second)
                {
                    diagnostic = "Live2D model3 Textures contains a duplicate path";
                    return false;
                }
                try
                {
                    asset::TextureImportRequest texture_request{};
                    texture_request.source_path = resolved;
                    texture_request.settings.semantic = data::TextureSemantic::Color;
                    const asset::CookedTexture cooked =
                        asset::TextureCooker{}.Cook(asset::TextureImporter{}.Import(texture_request));
                    resource.textures.push_back(
                        {".archive/" +
                         asset::ProductRelativePath(asset::ArchiveProductType::Texture,
                                                    cooked.product_hash, "texture")});
                    cooked_textures.push_back(cooked);
                }
                catch (const std::exception &error)
                {
                    diagnostic = "Live2D texture cooking failed: " + std::string(error.what());
                    return false;
                }
            }

            for (const auto &[key, value] : references.items())
            {
                if (key == "Moc" || key == "Textures")
                {
                    continue;
                }
                if (!AddReferenceValue(root, source.parent_path(), key, value, resource,
                                       diagnostic))
                {
                    return false;
                }
            }
            for (const asset::CookedTexture &cooked : cooked_textures)
            {
                asset::PublishCookedTextureProduct(request.archive_root, cooked);
            }
            return true;
        }
    }

    bool RegisterLive2DImporters(asset::ImportProviderRegistry &registry,
                                 std::string &diagnostic)
    {
        asset::ImportProviderDescriptor descriptor{};
        descriptor.id = "live2d";
        descriptor.version = 1;
        descriptor.kind = asset::ImportProviderKind::Custom;
        descriptor.source_suffixes = {"model3.json"};
        descriptor.callback = [](const asset::ImportProviderRequest &request)
        {
            asset::ImportProviderResult result{};
            auto product = std::make_shared<Live2DImportProduct>();
            if (!BuildResource(request, product->product, result.diagnostic))
            {
                if (result.diagnostic.empty())
                {
                    result.diagnostic = "Live2D source import failed without a diagnostic";
                }
                return result;
            }
            if (!SerializeLive2DProduct(product->product, product->product_bytes,
                                        result.diagnostic))
            {
                if (result.diagnostic.empty())
                {
                    result.diagnostic = "Live2D product serialization failed without a diagnostic";
                }
                return result;
            }
            result.product = std::make_shared<asset::TypedImportProduct<
                Live2DImportProduct, asset::ImportProviderKind::Custom>>(std::move(*product));
            result.status = asset::ImportProviderStatus::Imported;
            return result;
        };
        return registry.Register(std::move(descriptor), diagnostic);
    }
}
