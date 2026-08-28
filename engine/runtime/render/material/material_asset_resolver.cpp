#include "material_asset_resolver.h"

#include <array>
#include <filesystem>
#include <string>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/material.h"
#include "asset/shader_program.h"

namespace kpengine::render
{
    MaterialAssetResolver::MaterialAssetResolver(MaterialSystem &material_system)
        : material_system_(&material_system)
    {
    }

    MaterialResolution MaterialAssetResolver::Resolve(asset::AssetID material_asset,
                                                      MaterialInstanceHandle &out_instance)
    {
        if (!material_asset.IsValid() || material_asset.type != asset::AssetType::KPAT_Material)
        {
            return {MaterialResourceState::Failed, "static mesh source has an invalid material asset"};
        }

        const uint64_t key = material_asset.Pack();
        const auto cached = records_.find(key);
        if (cached != records_.end())
        {
            out_instance = cached->second.default_instance;
            return material_system_->GetInstanceResolution(out_instance);
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const auto material = asset_manager.GetResource<asset::MaterialResource>(material_asset);
        asset::Asset *const material_wrapper = asset_manager.GetAsset(material_asset);
        if (!material || !material_wrapper)
        {
            return {MaterialResourceState::Pending, "material asset is not loaded"};
        }

        const std::filesystem::path material_directory =
            std::filesystem::path(material_wrapper->GetPath()).parent_path();
        const auto load_reference = [&asset_manager, &material_directory](const std::string &reference)
        {
            const std::filesystem::path path = std::filesystem::path(reference).is_absolute()
                                                   ? std::filesystem::path(reference)
                                                   : material_directory / reference;
            return asset_manager.LoadSync(path.lexically_normal().string());
        };

        const asset::AssetID shader_program = load_reference(material->shader_path);
        if (!shader_program.IsValid() || shader_program.type != asset::AssetType::KPAT_ShaderProgram)
        {
            return {MaterialResourceState::Failed, "material shader program could not be loaded"};
        }

        MaterialTemplateDesc template_desc{};
        template_desc.shader_program = shader_program;
        template_desc.bindless_texture_table_compatible = true;
        template_desc.shading_model = MaterialShadingModel::Unlit;
        template_desc.pipeline_state.blend_mode =
            material->surface.blend_mode == asset::MaterialBlendMode::AlphaBlend
                ? MaterialBlendMode::AlphaBlend
                : MaterialBlendMode::Opaque;
        template_desc.pipeline_state.double_sided = material->surface.double_sided ||
                                                   material->surface.cull_mode == asset::MaterialCullMode::None;
        template_desc.pipeline_state.cull_mode =
            material->surface.cull_mode == asset::MaterialCullMode::Front
                ? MaterialCullMode::Front
                : MaterialCullMode::Back;

        for (const asset::MaterialParameterSource &parameter : material->parameters)
        {
            MaterialParameterDesc desc{};
            desc.name = parameter.name;
            switch (parameter.type)
            {
            case asset::MaterialParameterSourceType::Scalar:
                desc.default_value = std::get<float>(parameter.value);
                break;
            case asset::MaterialParameterSourceType::Vector4:
            {
                const auto &value = std::get<std::array<float, 4>>(parameter.value);
                desc.default_value = Vector4f{value[0], value[1], value[2], value[3]};
                break;
            }
            case asset::MaterialParameterSourceType::Texture:
            {
                if (parameter.name != "base_color_texture")
                {
                    return {MaterialResourceState::Failed,
                            "Material Asset V1 supports only base_color_texture"};
                }
                const asset::AssetID texture_asset =
                    load_reference(std::get<std::string>(parameter.value));
                if (!texture_asset.IsValid() || texture_asset.type != asset::AssetType::KPAT_Texture)
                {
                    return {MaterialResourceState::Failed, "material texture could not be loaded"};
                }
                desc.default_value = MaterialTextureSamplerValue{texture_asset, {}};
                desc.resource_binding = 2;
                break;
            }
            }
            template_desc.parameters.push_back(std::move(desc));
        }

        const MaterialTemplateHandle template_handle = material_system_->CreateTemplate(template_desc);
        if (!template_handle.IsValid())
        {
            return {MaterialResourceState::Failed, "material template creation failed"};
        }
        const MaterialInstanceHandle default_instance =
            material_system_->CreateInstance({template_handle, {}});
        if (!default_instance.IsValid())
        {
            (void)material_system_->DestroyTemplate(template_handle);
            return {MaterialResourceState::Failed, "material default-instance creation failed"};
        }

        records_.emplace(key, Record{template_handle, default_instance});
        out_instance = default_instance;
        return material_system_->GetInstanceResolution(default_instance);
    }

    void MaterialAssetResolver::Clear()
    {
        for (const auto &[key, record] : records_)
        {
            (void)key;
            (void)material_system_->DestroyInstance(record.default_instance);
            (void)material_system_->DestroyTemplate(record.template_handle);
        }
        records_.clear();
    }
}
