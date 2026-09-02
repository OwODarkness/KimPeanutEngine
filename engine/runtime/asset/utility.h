#ifndef KPENGINE_RUNTIME_ASSET_UTILITY_H
#define KPENGINE_RUNTIME_ASSET_UTILITY_H

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "common.h"
#include "config/path.h"
namespace kpengine::asset
{
    inline std::string CanonicalAssetPathKey(const std::string &path)
    {
        std::string key = path;
        std::replace(key.begin(), key.end(), '\\', '/');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    inline std::string GetFileExtension(const std::string &path)
    {
        auto pos = path.find_last_of('.');
        if (pos == std::string::npos)
            return "";
        std::string ext = path.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });
        return ext;
    }

    inline bool IsModelExtension(const std::string &ext)
    {
        static const std::vector<std::string> model_exts = {"obj", "fbx", "gltf"};
        return std::find(model_exts.begin(), model_exts.end(), ext) != model_exts.end();
    }

    inline bool IsTextureExtension(const std::string &ext)
    {
        static const std::vector<std::string> tex_exts = {"png", "jpg", "jpeg", "tga", "hdr"};
        return std::find(tex_exts.begin(), tex_exts.end(), ext) != tex_exts.end();
    }

    inline bool IsAudioExtension(const std::string &ext)
    {
        static const std::vector<std::string> audio_exts = {"wav", "mp3", "flac", "ogg"};
        return std::find(audio_exts.begin(), audio_exts.end(), ext) != audio_exts.end();
    }

    inline bool IsShaderProgramExtension(const std::string& ext)
    {
        return ext == "shader";
    }

    inline bool IsMaterialExtension(const std::string &ext)
    {
        return ext == "material";
    }

    inline bool IsLevelExtension(const std::string &ext)
    {
        return ext == "level";
    }

    inline bool IsShaderExtension(const std::string& ext)
    {
        static const std::vector<std::string> shader_exts = {"vert", "vs", "frag", "fs", "geom", "gs", "comp", "cs", "spv"};
        return std::find(shader_exts.begin(), shader_exts.end(), ext) != shader_exts.end();
    }

    inline AssetType ExtractAssetType(const std::string &ext)
    {
        if (IsModelExtension(ext))
        {
            return AssetType::KPAT_Model;
        }
        else if (IsTextureExtension(ext))
        {
            return AssetType::KPAT_Texture;
        }
        else if (IsAudioExtension(ext))
        {
            return AssetType::KPAT_Audio;
        }
        else if(IsShaderExtension(ext))
        {
            return AssetType::KPAT_Shader;
        }
        else if(IsShaderProgramExtension(ext))
        {
            return AssetType::KPAT_ShaderProgram;
        }
        else if (IsMaterialExtension(ext))
        {
            return AssetType::KPAT_Material;
        }
        else if (IsLevelExtension(ext))
        {
            return AssetType::KPAT_Level;
        }
        else
        {
            return AssetType::Undefined;
        }
    }

    inline std::string ExtractNameFromPath(const std::string &path)
    {
        size_t pos = path.find_last_of("/\\");
        std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);

        size_t dot_pos = filename.find_last_of('.');
        if (dot_pos != std::string::npos)
            filename = filename.substr(0, dot_pos);

        return filename;
    }

    inline std::string ExtractDirectoryFromPath(const std::string &path)
    {
        size_t pos = path.find_last_of("/\\");
        if (pos == std::string::npos)
            return "";

        return path.substr(0, pos + 1);
    }

    // Normalize a serialized Asset-root-relative reference without touching
    // the filesystem. Callers decide whether an additional format-specific
    // restriction (for example HDR-only environments) is required.
    inline bool NormalizeAssetRootRelativePath(const std::string &authored_path,
                                               AssetType expected_type,
                                               std::string &normalized)
    {
        if (authored_path.empty() || authored_path.find('\0') != std::string::npos)
        {
            return false;
        }

        std::string portable = authored_path;
        std::replace(portable.begin(), portable.end(), '\\', '/');
        if (portable.empty() || portable.front() == '/' ||
            (portable.size() >= 2 &&
             std::isalpha(static_cast<unsigned char>(portable[0])) && portable[1] == ':'))
        {
            return false;
        }

        std::vector<std::string> segments;
        std::size_t begin = 0;
        while (begin <= portable.size())
        {
            const std::size_t end = portable.find('/', begin);
            const std::string segment = portable.substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (segment == "..")
            {
                if (segments.empty())
                {
                    return false;
                }
                segments.pop_back();
            }
            else if (!segment.empty() && segment != ".")
            {
                if (segment.find(':') != std::string::npos)
                {
                    return false;
                }
                segments.push_back(segment);
            }
            if (end == std::string::npos)
            {
                break;
            }
            begin = end + 1;
        }

        if (segments.empty())
        {
            return false;
        }
        normalized = segments.front();
        for (std::size_t index = 1; index < segments.size(); ++index)
        {
            normalized += "/" + segments[index];
        }
        return ExtractAssetType(GetFileExtension(normalized)) == expected_type;
    }

    // Material references are authored relative to their owning material
    // file. Return the normalized absolute load path while preserving the
    // authored spelling in MaterialResource for diagnostics and tools.
    inline bool ResolveOwnedAssetPath(const std::string &owner_path,
                                      const std::string &authored_path,
                                      AssetType expected_type,
                                      std::string &resolved_path)
    {
        if (authored_path.empty() || authored_path.find('\0') != std::string::npos)
        {
            return false;
        }
        const std::filesystem::path reference{authored_path};
        // Material references are Asset-root-relative in effect. Reject both
        // ordinary absolute paths and Windows drive-qualified paths before
        // joining them to the owning material.
        if (reference.is_absolute() || reference.has_root_name() || reference.has_root_directory())
        {
            return false;
        }

        std::error_code error;
        const std::filesystem::path asset_root =
            std::filesystem::absolute(std::filesystem::path(GetAssetDirectory()), error)
                .lexically_normal();
        const std::filesystem::path owner =
            std::filesystem::absolute(std::filesystem::path(owner_path), error)
                .lexically_normal();
        if (error)
        {
            return false;
        }

        const std::filesystem::path resolved =
            (owner.parent_path() / reference).lexically_normal();
        const std::filesystem::path relative_to_asset_root =
            resolved.lexically_relative(asset_root);
        if (relative_to_asset_root.empty())
        {
            return false;
        }
        for (const std::filesystem::path &component : relative_to_asset_root)
        {
            if (component == ".." || component == relative_to_asset_root.root_name())
            {
                return false;
            }
        }
        if (ExtractAssetType(GetFileExtension(resolved.generic_string())) != expected_type)
        {
            return false;
        }
        resolved_path = resolved.generic_string();
        return true;
    }
}

#endif
