#include "shader_loader.h"
#include <fstream>
#include "nlohmann/json.hpp"
#include "utility.h"
#include "log/logger.h"
#include "shader.h"
#include "shader_meta.h"
#include "asset_manager.h"
namespace kpengine::asset
{
    ShaderFormat ParseFormat(const std::string &format)
    {
        if (format == "glsl")
            return ShaderFormat::GLSL;
        if (format == "spirv")
            return ShaderFormat::SPIRV;
        return ShaderFormat::Unknown;
    }

    ShaderStage ParseStage(const std::string &stage)
    {
        if (stage == "vertex")
            return ShaderStage::SHADER_STAGE_VERTEX;
        if (stage == "fragment")
            return ShaderStage::SHADER_STAGE_FRAGMENT;
        if (stage == "geometry")
            return ShaderStage::SHADER_STAGE_GEOMETRY;
        if (stage == "compute")
            return ShaderStage::SHADER_STAGE_COMPUTE;
        return ShaderStage::SHADER_STAGE_UNKNOW;
    }

    using json = nlohmann::json;
    bool ShaderLoader::Load(const std::string &path, AssetRegisterInfo &info)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            KP_LOG("ShaderLoaderLog", LOG_LEVEL_ERROR, "Failed to open %s", path.c_str());
            return false;
        }

        json json;
        file >> json;

        std::shared_ptr<ShaderMetaResource> shader_meta = std::make_shared<ShaderMetaResource>();
        info.type = AssetType::KPAT_ShaderMeta;
        info.name = ExtractNameFromPath(path);
        info.path = path;
        info.resource = shader_meta;

        for (const auto &item : json["shaders"])
        {
            std::shared_ptr<ShaderResource> shader = std::make_shared<ShaderResource>();
            shader->status = ShaderStatus::Uncompiled;
            std::string s_stage = item.value("stage", "");
            ShaderStage shader_stage = ParseStage(s_stage);
            if (shader_stage == ShaderStage::SHADER_STAGE_UNKNOW)
            {
                KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNNING,
                       "Unknown ShaderStage: %s", s_stage.c_str());
            }

            std::string s_format = item.value("format", "");
            ShaderFormat shader_format = ParseFormat(s_format);

            if (shader_format == ShaderFormat::Unknown)
            {
                KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNNING,
                       "Unknown ShaderFormat: %s", s_format.c_str());
            }
            if (!item.contains("file"))
            {
                KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNNING,
                       "Shader missing file field");
            }

            std::string s_file = item["file"].get<std::string>();

            std::string s_entry = item.value("entry", "main");


            shader->format = shader_format;
            shader->meta.stage = shader_stage;
            shader->meta.file = s_file;
            shader->meta.entry = s_entry;

            std::string dir = ExtractDirectoryFromPath(path);

            AssetRegisterInfo shader_register_info{};
            shader_register_info.type = AssetType::KPAT_Shader;
            shader_register_info.resource = shader;
            shader_register_info.name = s_file;
            shader_register_info.path = dir + s_file;

            AssetID shader_id = AssetManager::GetInstance().RegisterAsset(shader_register_info);
            
            shader_meta->BindData(shader_stage, shader_format, shader_id);
            info.dependencies.push_back(shader_id);
        }

        return true;
    }

}