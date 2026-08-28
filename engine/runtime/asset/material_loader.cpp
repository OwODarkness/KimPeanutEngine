#include "material_loader.h"

#include <fstream>
#include <initializer_list>
#include <memory>
#include <utility>
#include <nlohmann/json.hpp>

#include "log/logger.h"
#include "material.h"
#include "utility.h"

namespace kpengine::asset
{
    namespace
    {
        using json = nlohmann::json;

        constexpr int kMaterialVersion = 1;

        bool HasOnlyFields(const json &object, std::initializer_list<const char *> allowed)
        {
            for (const auto &[name, value] : object.items())
            {
                (void)value;
                bool known = false;
                for (const char *const allowed_name : allowed)
                {
                    if (name == allowed_name)
                    {
                        known = true;
                        break;
                    }
                }
                if (!known)
                {
                    return false;
                }
            }
            return true;
        }

        bool ParseSurface(const json &source, MaterialSurfaceSource &surface)
        {
            if (!source.is_object() ||
                !HasOnlyFields(source, {"shading_model", "blend_mode", "cull_mode", "double_sided"}))
            {
                return false;
            }

            const std::string shading_model = source.value("shading_model", std::string{});
            const std::string blend_mode = source.value("blend_mode", std::string{});
            const std::string cull_mode = source.value("cull_mode", std::string{});
            if (!source.contains("double_sided") || !source["double_sided"].is_boolean() ||
                shading_model != "unlit")
            {
                return false;
            }
            surface.shading_model = MaterialShadingModel::Unlit;

            if (blend_mode == "opaque")
            {
                surface.blend_mode = MaterialBlendMode::Opaque;
            }
            else if (blend_mode == "alpha_blend")
            {
                surface.blend_mode = MaterialBlendMode::AlphaBlend;
            }
            else
            {
                return false;
            }

            if (cull_mode == "none")
            {
                surface.cull_mode = MaterialCullMode::None;
            }
            else if (cull_mode == "back")
            {
                surface.cull_mode = MaterialCullMode::Back;
            }
            else if (cull_mode == "front")
            {
                surface.cull_mode = MaterialCullMode::Front;
            }
            else
            {
                return false;
            }
            surface.double_sided = source["double_sided"].get<bool>();
            return true;
        }

        bool ParseParameters(const json &source, std::vector<MaterialParameterSource> &parameters)
        {
            if (!source.is_object())
            {
                return false;
            }

            parameters.reserve(source.size());
            for (const auto &[name, value] : source.items())
            {
                if (name.empty())
                {
                    return false;
                }

                MaterialParameterSource parameter{};
                parameter.name = name;
                if (value.is_number())
                {
                    parameter.type = MaterialParameterSourceType::Scalar;
                    parameter.value = value.get<float>();
                }
                else if (value.is_string())
                {
                    const std::string texture_path = value.get<std::string>();
                    if (texture_path.empty())
                    {
                        return false;
                    }
                    parameter.type = MaterialParameterSourceType::Texture;
                    parameter.value = texture_path;
                }
                else if (value.is_array() && value.size() == 4)
                {
                    std::array<float, 4> vector{};
                    for (std::size_t index = 0; index < vector.size(); ++index)
                    {
                        if (!value[index].is_number())
                        {
                            return false;
                        }
                        vector[index] = value[index].get<float>();
                    }
                    parameter.type = MaterialParameterSourceType::Vector4;
                    parameter.value = vector;
                }
                else
                {
                    return false;
                }
                parameters.push_back(std::move(parameter));
            }
            return true;
        }
    }

    bool MaterialLoader::Load(const std::string &path, AssetRegisterInfo &info)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR, "Failed to open %s", path.c_str());
            return false;
        }

        try
        {
            json source;
            file >> source;
            if (!source.is_object() ||
                !HasOnlyFields(source, {"version", "shader", "surface", "parameters"}) ||
                !source.contains("version") || !source["version"].is_number_integer() ||
                source["version"].get<int>() != kMaterialVersion || !source.contains("shader") ||
                !source["shader"].is_string() || source["shader"].get<std::string>().empty() ||
                !source.contains("surface") || !source.contains("parameters"))
            {
                KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR, "Invalid material schema in %s", path.c_str());
                return false;
            }

            auto material = std::make_shared<MaterialResource>();
            material->shader_path = source["shader"].get<std::string>();
            if (!ParseSurface(source["surface"], material->surface) ||
                !ParseParameters(source["parameters"], material->parameters))
            {
                KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR, "Invalid material values in %s", path.c_str());
                return false;
            }

            info.type = AssetType::KPAT_Material;
            info.name = ExtractNameFromPath(path);
            info.path = path;
            info.resource = std::move(material);
            return true;
        }
        catch (const json::exception &exception)
        {
            KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR, "Failed to parse %s: %s", path.c_str(),
                   exception.what());
            return false;
        }
    }
}
