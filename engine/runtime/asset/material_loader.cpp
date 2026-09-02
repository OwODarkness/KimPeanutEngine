#include "material_loader.h"

#include <array>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
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

        // V1 = unlit-only. V2 adds the standard_pbr shading model and explicit
        // PBR semantic names (base_color/normal/metallic/roughness/occlusion/
        // emissive); parameter parsing is name-agnostic in both.
        constexpr int kMaterialVersionLatest = 2;
        constexpr int kMaterialVersionMin = 1;

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

        bool ParseSurface(int version, const json &source, MaterialSurfaceSource &surface)
        {
            if (!source.is_object() ||
                !HasOnlyFields(source, {"shading_model", "blend_mode", "cull_mode", "double_sided"}))
            {
                return false;
            }

            const std::string shading_model = source.value("shading_model", std::string{});
            const std::string blend_mode = source.value("blend_mode", std::string{});
            const std::string cull_mode = source.value("cull_mode", std::string{});
            if (!source.contains("double_sided") || !source["double_sided"].is_boolean())
            {
                return false;
            }
            // V1 is unlit-only; V2 also accepts standard_pbr.
            if (shading_model == "unlit")
            {
                surface.shading_model = MaterialShadingModel::Unlit;
            }
            else if (version >= 2 && shading_model == "standard_pbr")
            {
                surface.shading_model = MaterialShadingModel::StandardPbr;
            }
            else
            {
                return false;
            }

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

        bool IsFiniteInRange(float value, float minimum, float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        }

        bool ValidateStandardPbrParameters(const std::vector<MaterialParameterSource> &parameters)
        {
            for (const MaterialParameterSource &parameter : parameters)
            {
                if (parameter.name == "base_color")
                {
                    if (parameter.type != MaterialParameterSourceType::Vector4)
                    {
                        return false;
                    }
                    const auto &value = std::get<std::array<float, 4>>(parameter.value);
                    for (const float component : value)
                    {
                        if (!IsFiniteInRange(component, 0.0f, 1.0f))
                        {
                            return false;
                        }
                    }
                }
                else if (parameter.name == "emissive")
                {
                    if (parameter.type != MaterialParameterSourceType::Vector4)
                    {
                        return false;
                    }
                    const auto &value = std::get<std::array<float, 4>>(parameter.value);
                    for (const float component : value)
                    {
                        if (!std::isfinite(component) || component < 0.0f)
                        {
                            return false;
                        }
                    }
                }
                else if (parameter.name == "metallic" || parameter.name == "roughness" ||
                         parameter.name == "occlusion")
                {
                    if (parameter.type != MaterialParameterSourceType::Scalar ||
                        !IsFiniteInRange(std::get<float>(parameter.value), 0.0f, 1.0f))
                    {
                        return false;
                    }
                }
                else if (parameter.name == "base_color_texture" ||
                         parameter.name == "normal_texture" ||
                         parameter.name == "metallic_texture" ||
                         parameter.name == "roughness_texture" ||
                         parameter.name == "occlusion_texture")
                {
                    if (parameter.type != MaterialParameterSourceType::Texture)
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
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
                !source.contains("shader") ||
                !source["shader"].is_string() || source["shader"].get<std::string>().empty() ||
                !source.contains("surface") || !source.contains("parameters"))
            {
                KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR, "Invalid material schema in %s", path.c_str());
                return false;
            }

            const int version = source["version"].get<int>();
            if (version < kMaterialVersionMin || version > kMaterialVersionLatest)
            {
                KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR,
                       "Unsupported material version %d in %s", version, path.c_str());
                return false;
            }

            auto material = std::make_shared<MaterialResource>();
            material->version = version;
            material->shader_path = source["shader"].get<std::string>();
            if (!ParseSurface(version, source["surface"], material->surface) ||
                !ParseParameters(source["parameters"], material->parameters) ||
                (material->surface.shading_model == MaterialShadingModel::StandardPbr &&
                 !ValidateStandardPbrParameters(material->parameters)))
            {
                KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR, "Invalid material values in %s", path.c_str());
                return false;
            }

            std::string resolved_shader_path;
            if (!ResolveOwnedAssetPath(path, material->shader_path,
                                       AssetType::KPAT_ShaderProgram, resolved_shader_path))
            {
                KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR,
                       "Invalid shader reference in %s", path.c_str());
                return false;
            }
            material->shader_dependency_index =
                static_cast<uint32_t>(info.dependency_requests.size());
            info.dependency_requests.push_back(
                {std::move(resolved_shader_path), AssetType::KPAT_ShaderProgram});

            for (MaterialParameterSource &parameter : material->parameters)
            {
                if (parameter.type != MaterialParameterSourceType::Texture)
                {
                    continue;
                }
                const std::string &authored_texture =
                    std::get<std::string>(parameter.value);
                std::string resolved_texture_path;
                if (!ResolveOwnedAssetPath(path, authored_texture, AssetType::KPAT_Texture,
                                           resolved_texture_path))
                {
                    KP_LOG("MaterialLoaderLog", LOG_LEVEL_ERROR,
                           "Invalid texture reference for %s in %s",
                           parameter.name.c_str(), path.c_str());
                    return false;
                }
                parameter.dependency_index =
                    static_cast<uint32_t>(info.dependency_requests.size());
                info.dependency_requests.push_back(
                    {std::move(resolved_texture_path), AssetType::KPAT_Texture});
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
