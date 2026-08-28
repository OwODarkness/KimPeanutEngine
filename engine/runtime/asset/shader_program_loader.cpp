#include "shader_program_loader.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include "utility.h"
#include "log/logger.h"
#include "shader.h"
#include "shader_program.h"
#include "asset_manager.h"

namespace kpengine::asset
{
    ShaderFormat ParseFormat(const std::string &format)
    {
        if (format == "glsl")
            return ShaderFormat::SHADER_FORMAT_GLSL;
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

    constexpr int kShaderProgramVersion = 1;
    bool ShaderProgramLoader::Load(const std::string &path, AssetRegisterInfo &info)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            KP_LOG("ShaderLoaderLog", LOG_LEVEL_ERROR, "Failed to open %s", path.c_str());
            return false;
        }

        json json;
        file >> json;

        int version = json.value("version", 1);
        if (version != kShaderProgramVersion)
        {
            KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNING,
                   "%s uses shader program version %d, loader supports %d",
                   path.c_str(), version, kShaderProgramVersion);
        }

        std::shared_ptr<ShaderProgramResource> shader_program = std::make_shared<ShaderProgramResource>();
        info.type = AssetType::KPAT_ShaderProgram;
        info.name = ExtractNameFromPath(path);
        info.path = path;
        info.resource = shader_program;

        using VariantDefines = std::pair<ShaderProgramVariant, std::vector<std::string>>;
        std::vector<VariantDefines> variants{{ShaderProgramVariant::Bound, {}}};
        if (json.contains("variants") && json["variants"].is_array())
        {
            variants.clear();
            for (const auto &variant : json["variants"])
            {
                const std::string name = variant.value("name", std::string{});
                const ShaderProgramVariant kind = name == "bindless"
                                                      ? ShaderProgramVariant::Bindless
                                                      : ShaderProgramVariant::Bound;
                std::vector<std::string> defines;
                if (variant.contains("defines") && variant["defines"].is_array())
                {
                    for (const auto &define : variant["defines"])
                    {
                        if (define.is_string())
                        {
                            defines.push_back(define.get<std::string>());
                        }
                    }
                }
                variants.emplace_back(kind, std::move(defines));
            }
            if (variants.empty())
            {
                variants.emplace_back(ShaderProgramVariant::Bound, std::vector<std::string>{});
            }
        }

        for (const auto &item : json["shaders"])
        {
            std::string s_stage = item.value("stage", "");
            ShaderStage shader_stage = ParseStage(s_stage);
            if (shader_stage == ShaderStage::SHADER_STAGE_UNKNOW)
            {
                KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNING,
                       "Unknown ShaderStage: %s", s_stage.c_str());
            }

            std::string s_format = item.value("format", "");
            ShaderFormat shader_format = ParseFormat(s_format);

            if (shader_format == ShaderFormat::Unknown)
            {
                KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNING,
                       "Unknown ShaderFormat: %s", s_format.c_str());
            }
            if (!item.contains("file"))
            {
                KP_LOG("ShaderLoaderLog", LOG_LEVEL_WARNING,
                       "Shader missing file field");
            }

            std::string s_file = item["file"].get<std::string>();

            std::string s_entry = item.value("entry", "main");

            std::vector<std::string> defines;
            if (item.contains("defines") && item["defines"].is_array())
            {
                for (const auto &d : item["defines"])
                {
                    if (d.is_string())
                    {
                        defines.push_back(d.get<std::string>());
                    }
                }
            }

            std::string dir = ExtractDirectoryFromPath(path);
            std::string abs_path = dir + s_file;

            for (const auto &[variant, variant_defines] : variants)
            {
                std::shared_ptr<ShaderResource> shader = std::make_shared<ShaderResource>();
                shader->status = ShaderStatus::Uncompiled;
                shader->format = shader_format;
                shader->variant = variant;
                shader->desc.stage = shader_stage;
                shader->desc.file = abs_path;
                shader->desc.entry = s_entry;
                shader->desc.defines = defines;
                shader->desc.defines.insert(shader->desc.defines.end(), variant_defines.begin(),
                                            variant_defines.end());

                AssetRegisterInfo shader_register_info{};
                shader_register_info.type = AssetType::KPAT_Shader;
                shader_register_info.resource = std::move(shader);
                shader_register_info.name = s_file;
                shader_register_info.path = abs_path;

                const AssetID shader_id = AssetManager::GetInstance().RegisterAsset(shader_register_info);
                shader_program->BindData(shader_stage, shader_format, shader_id, variant);
                info.dependencies.push_back(shader_id);
            }
        }

        return true;
    }

}
