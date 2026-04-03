#ifndef KPENGINE_RUNTIME_ASSET_UTILITY_H
#define KPENGINE_RUNTIME_ASSET_UTILITY_H

#include <string>
#include <vector>
#include <algorithm>
#include "common.h"
namespace kpengine::asset
{
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
        static const std::vector<std::string> tex_exts = {"png", "jpg", "jpeg", "tga"};
        return std::find(tex_exts.begin(), tex_exts.end(), ext) != tex_exts.end();
    }

    inline bool IsAudioExtension(const std::string &ext)
    {
        static const std::vector<std::string> audio_exts = {"png", "jpg", "jpeg", "tga"};
        return std::find(audio_exts.begin(), audio_exts.end(), ext) != audio_exts.end();
    }

    inline bool IsShaderMetaExtension(const std::string& ext)
    {
        return ext == "shader";
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
        else if(IsShaderMetaExtension(ext))
        {
            return AssetType::KPAT_ShaderMeta;
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
}

#endif