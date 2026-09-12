#include "live2d_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

        bool HasSuffix(const std::string &value, std::string_view suffix)
        {
            return value.size() >= suffix.size() &&
                   value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
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

        bool BuildTypedAnimationData(const std::filesystem::path &root,
                                      const std::filesystem::path &source_directory,
                                      const Json &document,
                                      Live2DProductData &resource,
                                      std::string &diagnostic)
        {
            constexpr std::size_t kMaxEntries = 4096;
            const Json &references = document["FileReferences"];
            if (references.contains("Motions"))
            {
                if (!references["Motions"].is_object())
                {
                    diagnostic = "Live2D Motions must be an object";
                    return false;
                }
                std::vector<std::string> groups;
                for (const auto &[group_name, entries] : references["Motions"].items())
                {
                    groups.push_back(group_name);
                    if (group_name.empty() ||
                        group_name.find(static_cast<char>(0)) != std::string::npos ||
                        !entries.is_array())
                    {
                        diagnostic = "Live2D motion group is invalid";
                        return false;
                    }
                    if (entries.size() > kMaxEntries)
                    {
                        diagnostic = "Live2D motion group has too many entries";
                        return false;
                    }
                }
                if (groups.size() > kMaxEntries)
                {
                    diagnostic = "Live2D has too many motion groups";
                    return false;
                }
                std::sort(groups.begin(), groups.end());
                for (const std::string &group_name : groups)
                {
                    const Json &entries = references["Motions"][group_name];
                    for (std::size_t index = 0; index < entries.size(); ++index)
                    {
                        const Json &entry = entries[index];
                        if (!entry.is_object() || !entry.contains("File") ||
                            !entry["File"].is_string())
                        {
                            diagnostic = "Live2D motion entry requires a File string";
                            return false;
                        }
                        Live2DAuthoredMotion motion{};
                        motion.group = group_name;
                        motion.index = static_cast<std::uint32_t>(index);
                        const std::string file = entry["File"].get<std::string>();
                        std::filesystem::path resolved;
                        std::string relative;
                        if (!ResolveSourcePath(root, source_directory, file, resolved, relative,
                                               diagnostic) ||
                            !HasSuffix(Lowercase(resolved.filename().generic_string()),
                                       ".motion3.json") ||
                            !ReadBytes(resolved, motion.motion_bytes, diagnostic))
                        {
                            if (diagnostic.empty())
                            {
                                diagnostic = "Live2D motion File is invalid";
                            }
                            return false;
                        }
                        Json motion_json;
                        if (!ReadJson(resolved, motion_json, diagnostic) ||
                            !motion_json.is_object())
                        {
                            if (diagnostic.empty())
                            {
                                diagnostic = "Live2D motion JSON must be an object";
                            }
                            return false;
                        }
                        const auto read_fade = [&](const char *key, bool &present,
                                                   double &value) -> bool
                        {
                            if (!entry.contains(key))
                            {
                                return true;
                            }
                            if (!entry[key].is_number())
                            {
                                diagnostic = std::string("Live2D motion ") + key +
                                             " must be a finite non-negative number";
                                return false;
                            }
                            value = entry[key].get<double>();
                            if (!std::isfinite(value) || value < 0.0)
                            {
                                diagnostic = std::string("Live2D motion ") + key +
                                             " must be a finite non-negative number";
                                return false;
                            }
                            present = true;
                            return true;
                        };
                        if (!read_fade("FadeInTime", motion.has_fade_in, motion.fade_in_time) ||
                            !read_fade("FadeOutTime", motion.has_fade_out,
                                       motion.fade_out_time))
                        {
                            return false;
                        }
                        if (entry.contains("Sound"))
                        {
                            if (!entry["Sound"].is_string())
                            {
                                diagnostic = "Live2D motion Sound must be a string";
                                return false;
                            }
                            if (!ResolveSourcePath(root, source_directory,
                                                   entry["Sound"].get<std::string>(),
                                                   resolved, relative, diagnostic) ||
                                !ReadBytes(resolved, motion.sound_bytes, diagnostic))
                            {
                                return false;
                            }
                            motion.has_sound = true;
                        }
                        resource.motions.push_back(std::move(motion));
                    }
                }
            }

            if (references.contains("Expressions"))
            {
                if (!references["Expressions"].is_array())
                {
                    diagnostic = "Live2D Expressions must be an array";
                    return false;
                }
                if (references["Expressions"].size() > kMaxEntries)
                {
                    diagnostic = "Live2D has too many expressions";
                    return false;
                }
                std::set<std::string> names;
                for (const Json &entry : references["Expressions"])
                {
                    if (!entry.is_object() || !entry.contains("Name") ||
                        !entry["Name"].is_string() || !entry.contains("File") ||
                        !entry["File"].is_string())
                    {
                        diagnostic = "Live2D expression entry requires Name and File strings";
                        return false;
                    }
                    Live2DAuthoredExpression expression{};
                    expression.name = entry["Name"].get<std::string>();
                    if (expression.name.empty() ||
                        expression.name.find(static_cast<char>(0)) != std::string::npos ||
                        !names.insert(expression.name).second)
                    {
                        diagnostic = "Live2D expression names must be unique and non-empty";
                        return false;
                    }
                    std::filesystem::path resolved;
                    std::string relative;
                    if (!ResolveSourcePath(root, source_directory,
                                           entry["File"].get<std::string>(), resolved, relative,
                                           diagnostic) ||
                        !HasSuffix(Lowercase(resolved.filename().generic_string()),
                                   ".exp3.json") ||
                        !ReadBytes(resolved, expression.expression_bytes, diagnostic))
                    {
                        if (diagnostic.empty())
                        {
                            diagnostic = "Live2D expression File is invalid";
                        }
                        return false;
                    }
                    Json expression_json;
                    if (!ReadJson(resolved, expression_json, diagnostic) ||
                        !expression_json.is_object())
                    {
                        if (diagnostic.empty())
                        {
                            diagnostic = "Live2D expression JSON must be an object";
                        }
                        return false;
                    }
                    resource.expressions.push_back(std::move(expression));
                }
            }

            if (document.contains("Groups"))
            {
                if (!document["Groups"].is_array())
                {
                    diagnostic = "Live2D Groups must be an array";
                    return false;
                }
                if (document["Groups"].size() > kMaxEntries)
                {
                    diagnostic = "Live2D has too many parameter groups";
                    return false;
                }
                std::set<std::string> names;
                for (const Json &entry : document["Groups"])
                {
                    if (!entry.is_object() || !entry.contains("Target") ||
                        !entry["Target"].is_string() ||
                        entry["Target"].get<std::string>() != "Parameter" ||
                        !entry.contains("Name") || !entry["Name"].is_string() ||
                        !entry.contains("Ids") || !entry["Ids"].is_array())
                    {
                        diagnostic = "Live2D Groups contains an unsupported parameter target";
                        return false;
                    }
                    Live2DParameterGroup group{};
                    group.target = entry["Target"].get<std::string>();
                    group.name = entry["Name"].get<std::string>();
                    if (group.name.empty() ||
                        group.name.find(static_cast<char>(0)) != std::string::npos ||
                        !names.insert(group.name).second || entry["Ids"].empty())
                    {
                        diagnostic = "Live2D parameter group names must be unique and non-empty";
                        return false;
                    }
                    std::set<std::string> ids;
                    for (const Json &id : entry["Ids"])
                    {
                        if (!id.is_string() || id.get<std::string>().empty() ||
                            id.get<std::string>().find(static_cast<char>(0)) != std::string::npos ||
                            !ids.insert(id.get<std::string>()).second)
                        {
                            diagnostic = "Live2D parameter group IDs must be unique strings";
                            return false;
                        }
                        group.ids.push_back(id.get<std::string>());
                    }
                    resource.parameter_groups.push_back(std::move(group));
                }
            }
            return true;
        }

        bool BuildResource(const asset::ImportProviderRequest &request,
                           Live2DProductData &resource,
                           std::vector<asset::CookedTexture> &cooked_textures,
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
            resource.product_version = 2;
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

            if (!BuildTypedAnimationData(root, source.parent_path(), document, resource, diagnostic))
            {
                return false;
            }

            for (const auto &[key, value] : references.items())
            {
                if (key == "Moc" || key == "Textures" || key == "Motions" ||
                    key == "Expressions")
                {
                    continue;
                }
                if (!AddReferenceValue(root, source.parent_path(), key, value, resource,
                                       diagnostic))
                {
                    return false;
                }
            }
            return true;
        }
    }

    bool RegisterLive2DImporters(asset::ImportProviderRegistry &registry,
                                 std::string &diagnostic)
    {
        asset::ImportProviderDescriptor descriptor{};
        descriptor.id = "live2d";
        descriptor.version = 2;
        descriptor.kind = asset::ImportProviderKind::Custom;
        descriptor.source_suffixes = {"model3.json"};
        descriptor.callback = [](const asset::ImportProviderRequest &request)
        {
            asset::ImportProviderResult result{};
            auto product = std::make_shared<Live2DImportProduct>();
            std::vector<asset::CookedTexture> cooked_textures;
            if (!BuildResource(request, product->product, cooked_textures, result.diagnostic))
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
            for (const asset::CookedTexture &cooked : cooked_textures)
            {
                asset::PublishCookedTextureProduct(request.archive_root, cooked);
            }
            result.product = std::make_shared<asset::TypedImportProduct<
                Live2DImportProduct, asset::ImportProviderKind::Custom>>(std::move(*product));
            result.status = asset::ImportProviderStatus::Imported;
            return result;
        };
        return registry.Register(std::move(descriptor), diagnostic);
    }
}
