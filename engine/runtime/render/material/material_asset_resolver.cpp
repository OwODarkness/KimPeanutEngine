#include "material_asset_resolver.h"

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "asset/material.h"
#include "asset/shader_program.h"

namespace kpengine::render
{
    namespace
    {
        // StandardPbr texture descriptor bindings. Base color keeps the V1 slot
        // (2); 4 is reserved for the D5 frame lighting block; the PBR maps take
        // the free high slots. Mirrors the GBuffer pipeline's descriptor layout
        // in render_resource_resolver.cpp.
        constexpr uint32_t kBindingBaseColorTexture = 2;
        constexpr uint32_t kBindingNormalTexture = 5;
        constexpr uint32_t kBindingMetallicTexture = 6;
        constexpr uint32_t kBindingRoughnessTexture = 7;
        constexpr uint32_t kBindingOcclusionTexture = 8;

        bool IsKnownSemanticName(const std::string &name)
        {
            return name == "base_color" || name == "base_color_texture" ||
                   name == "normal_texture" || name == "metallic" || name == "metallic_texture" ||
                   name == "roughness" || name == "roughness_texture" || name == "occlusion" ||
                   name == "occlusion_texture" || name == "emissive" || name == "normal_scale";
        }

        Vector4f AsVector4(const asset::MaterialParameterSource &parameter)
        {
            const auto &value = std::get<std::array<float, 4>>(parameter.value);
            return Vector4f{value[0], value[1], value[2], value[3]};
        }

        float ChannelSelector(asset::MaterialTextureChannel channel)
        {
            switch (channel)
            {
            case asset::MaterialTextureChannel::Red: return 0.0f;
            case asset::MaterialTextureChannel::Green: return 1.0f;
            case asset::MaterialTextureChannel::Blue: return 2.0f;
            case asset::MaterialTextureChannel::Alpha: return 3.0f;
            case asset::MaterialTextureChannel::Rgba: return 0.0f;
            }
            return 0.0f;
        }
    }

    MaterialAssetResolver::MaterialAssetResolver(
        MaterialSystem &material_system,
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets)
        : material_system_(&material_system), prepared_assets_(std::move(prepared_assets))
    {
        if (prepared_assets_)
        {
            default_white_ = prepared_assets_->GetBuiltIn(
                BuiltInRenderAsset::DefaultWhiteTexture);
            default_flat_normal_ = prepared_assets_->GetBuiltIn(
                BuiltInRenderAsset::DefaultFlatNormalTexture);
        }
    }

    MaterialResolution MaterialAssetResolver::Resolve(asset::AssetID material_asset,
                                                      MaterialInstanceHandle &out_instance)
    {
        if (!material_asset.IsValid() || material_asset.type != asset::AssetType::KPAT_Material)
        {
            return {MaterialResourceState::Failed, "static mesh source has an invalid material asset"};
        }
        if (!prepared_assets_)
        {
            return {MaterialResourceState::Pending, "render asset catalog is not published"};
        }

        const uint64_t key = material_asset.Pack();
        const auto cached = records_.find(key);
        if (cached != records_.end())
        {
            out_instance = cached->second.default_instance;
            return material_system_->GetInstanceResolution(out_instance);
        }

        const auto material = prepared_assets_ != nullptr
                                  ? prepared_assets_->Get<asset::MaterialResource>(material_asset)
                                  : nullptr;
        if (!material)
        {
            return {MaterialResourceState::Pending, "material asset is not loaded"};
        }

        const asset::AssetID shader_program = prepared_assets_->ResolveDependency(
            material_asset, material->shader_dependency_index,
            asset::AssetType::KPAT_ShaderProgram);
        if (!shader_program.IsValid() || shader_program.type != asset::AssetType::KPAT_ShaderProgram)
        {
            return {MaterialResourceState::Pending,
                    "material shader program dependency is not ready"};
        }

        MaterialTemplateDesc template_desc{};
        template_desc.shader_program = shader_program;
        template_desc.pipeline_state.blend_mode =
            (material->surface.blend_mode == asset::MaterialBlendMode::AlphaBlend ||
             material->surface.alpha_mode == asset::MaterialAlphaMode::Blend)
                ? MaterialBlendMode::AlphaBlend
                : MaterialBlendMode::Opaque;
        template_desc.pipeline_state.double_sided = material->surface.double_sided ||
                                                   material->surface.cull_mode == asset::MaterialCullMode::None;
        template_desc.pipeline_state.cull_mode =
            material->surface.cull_mode == asset::MaterialCullMode::Front
                ? MaterialCullMode::Front
                : MaterialCullMode::Back;

        if (material->surface.shading_model == asset::MaterialShadingModel::StandardPbr)
        {
            if (!BuildStandardPbrTemplate(material_asset, *material, template_desc))
            {
                return {MaterialResourceState::Failed,
                        "standard_pbr material semantics or references are invalid"};
            }
        }
        else
        {
            // Material Asset V1 unlit passthrough: arbitrary parameter names,
            // base_color_texture on binding 2 only.
            template_desc.bindless_texture_table_compatible = true;
            template_desc.shading_model = MaterialShadingModel::Unlit;

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
                    desc.default_value = AsVector4(parameter);
                    break;
                case asset::MaterialParameterSourceType::Texture:
                {
                    if (parameter.name != "base_color_texture")
                    {
                        return {MaterialResourceState::Failed,
                                "Material Asset V1 supports only base_color_texture"};
                    }
                    const asset::AssetID texture_asset = prepared_assets_->ResolveDependency(
                        material_asset, parameter.dependency_index, asset::AssetType::KPAT_Texture);
                    if (!texture_asset.IsValid() || texture_asset.type != asset::AssetType::KPAT_Texture)
                    {
                        return {MaterialResourceState::Pending,
                                "material texture dependency is not ready"};
                    }
                    const asset::AssetID block_compressed_texture_asset =
                        parameter.block_compressed_dependency_index ==
                                std::numeric_limits<uint32_t>::max()
                            ? asset::AssetID{}
                            : prepared_assets_->ResolveDependency(
                                  material_asset,
                                  parameter.block_compressed_dependency_index,
                                  asset::AssetType::KPAT_Texture);
                    desc.default_value = MaterialTextureSamplerValue{
                        texture_asset, block_compressed_texture_asset, {}};
                    desc.resource_binding = 2;
                    break;
                }
                }
                template_desc.parameters.push_back(std::move(desc));
            }
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

    bool MaterialAssetResolver::BuildStandardPbrTemplate(
        asset::AssetID material_asset, const asset::MaterialResource &material,
        MaterialTemplateDesc &template_desc)
    {
        // Collect authored semantics. Unknown names or type mismatches fail.
        std::optional<Vector4f> base_color;
        std::optional<asset::AssetID> base_color_texture;
        std::optional<asset::AssetID> base_color_block_texture;
        std::optional<MaterialTextureColorSpace> base_color_color_space;
        std::optional<asset::AssetID> normal_texture;
        std::optional<asset::AssetID> normal_block_texture;
        std::optional<MaterialTextureColorSpace> normal_color_space;
        std::optional<float> metallic;
        std::optional<asset::AssetID> metallic_texture;
        std::optional<asset::AssetID> metallic_block_texture;
        std::optional<MaterialTextureColorSpace> metallic_color_space;
        std::optional<float> roughness;
        std::optional<asset::AssetID> roughness_texture;
        std::optional<asset::AssetID> roughness_block_texture;
        std::optional<MaterialTextureColorSpace> roughness_color_space;
        std::optional<float> occlusion;
        std::optional<asset::AssetID> occlusion_texture;
        std::optional<asset::AssetID> occlusion_block_texture;
        std::optional<MaterialTextureColorSpace> occlusion_color_space;
        std::optional<Vector4f> emissive;
        std::optional<float> normal_scale;
        float metallic_channel = 0.0f;
        float roughness_channel = 0.0f;
        float occlusion_channel = 0.0f;

        for (const asset::MaterialParameterSource &parameter : material.parameters)
        {
            const std::string &name = parameter.name;
            if (name == "base_color")
            {
                if (parameter.type != asset::MaterialParameterSourceType::Vector4) return false;
                base_color = AsVector4(parameter);
            }
            else if (name == "emissive")
            {
                if (parameter.type != asset::MaterialParameterSourceType::Vector4) return false;
                emissive = AsVector4(parameter);
            }
            else if (name == "metallic" || name == "roughness" || name == "occlusion")
            {
                if (parameter.type != asset::MaterialParameterSourceType::Scalar) return false;
                const float value = std::get<float>(parameter.value);
                if (name == "metallic") metallic = value;
                else if (name == "roughness") roughness = value;
                else occlusion = value;
            }
            else if (name == "normal_scale")
            {
                if (parameter.type != asset::MaterialParameterSourceType::Scalar) return false;
                normal_scale = std::get<float>(parameter.value);
            }
            else if (name == "base_color_texture" || name == "normal_texture" ||
                     name == "metallic_texture" || name == "roughness_texture" ||
                     name == "occlusion_texture")
            {
                if (parameter.type != asset::MaterialParameterSourceType::Texture) return false;
                const asset::AssetID texture_asset = prepared_assets_->ResolveDependency(
                    material_asset, parameter.dependency_index, asset::AssetType::KPAT_Texture);
                if (!texture_asset.IsValid() || texture_asset.type != asset::AssetType::KPAT_Texture)
                {
                    return false;
                }
                const asset::AssetID block_compressed_texture_asset =
                    parameter.block_compressed_dependency_index ==
                            std::numeric_limits<uint32_t>::max()
                        ? asset::AssetID{}
                        : prepared_assets_->ResolveDependency(
                              material_asset,
                              parameter.block_compressed_dependency_index,
                              asset::AssetType::KPAT_Texture);
                const MaterialTextureColorSpace color_space =
                    parameter.texture_color_space == asset::MaterialTextureColorSpace::Srgb
                        ? MaterialTextureColorSpace::Srgb
                        : MaterialTextureColorSpace::Linear;
                if (name == "base_color_texture")
                {
                    base_color_texture = texture_asset;
                    base_color_block_texture = block_compressed_texture_asset;
                    base_color_color_space = color_space;
                }
                else if (name == "normal_texture")
                {
                    normal_texture = texture_asset;
                    normal_block_texture = block_compressed_texture_asset;
                    normal_color_space = color_space;
                }
                else if (name == "metallic_texture")
                {
                    metallic_texture = texture_asset;
                    metallic_block_texture = block_compressed_texture_asset;
                    metallic_color_space = color_space;
                    metallic_channel = ChannelSelector(parameter.texture_channel);
                }
                else if (name == "roughness_texture")
                {
                    roughness_texture = texture_asset;
                    roughness_block_texture = block_compressed_texture_asset;
                    roughness_color_space = color_space;
                    roughness_channel = ChannelSelector(parameter.texture_channel);
                }
                else
                {
                    occlusion_texture = texture_asset;
                    occlusion_block_texture = block_compressed_texture_asset;
                    occlusion_color_space = color_space;
                    occlusion_channel = ChannelSelector(parameter.texture_channel);
                }
            }
            else
            {
                return false;
            }
        }

        const asset::AssetID white = default_white_;
        const asset::AssetID flat_normal = default_flat_normal_;
        if (!white.IsValid() || !flat_normal.IsValid())
        {
            return false;
        }

        // Canonical order is the constant-block ABI (see pbr_gbuffer.frag and
        // the asset README): base_color@0, metallic@16, roughness@20,
        // occlusion@24, emissive@32. Texture-wins-over-scalar: an authored
        // texture forces its scalar constant to the identity multiplier.
        const auto add_scalar = [&template_desc](std::string name, float value)
        {
            MaterialParameterDesc desc{};
            desc.name = std::move(name);
            desc.default_value = value;
            template_desc.parameters.push_back(std::move(desc));
        };
        const auto add_vector = [&template_desc](std::string name, Vector4f value)
        {
            MaterialParameterDesc desc{};
            desc.name = std::move(name);
            desc.default_value = value;
            template_desc.parameters.push_back(std::move(desc));
        };
        const auto add_texture = [&template_desc](std::string name, uint32_t binding,
                                                  MaterialTextureColorSpace color_space,
                                                  asset::AssetID texture_asset,
                                                  asset::AssetID block_compressed_texture_asset)
        {
            MaterialParameterDesc desc{};
            desc.name = std::move(name);
            desc.default_value = MaterialTextureSamplerValue{
                texture_asset, block_compressed_texture_asset, {}, color_space};
            desc.resource_binding = binding;
            template_desc.parameters.push_back(std::move(desc));
        };

        add_vector("base_color", base_color_texture ? Vector4f{1.f, 1.f, 1.f, 1.f}
                                                    : base_color.value_or(Vector4f{1.f, 1.f, 1.f, 1.f}));
        add_texture("base_color_texture", kBindingBaseColorTexture,
                    base_color_color_space.value_or(MaterialTextureColorSpace::Srgb),
                    base_color_texture.value_or(white),
                    base_color_block_texture.value_or(asset::AssetID{}));
        add_texture("normal_texture", kBindingNormalTexture,
                    normal_color_space.value_or(MaterialTextureColorSpace::Linear),
                    normal_texture.value_or(flat_normal),
                    normal_block_texture.value_or(asset::AssetID{}));
        add_scalar("metallic", metallic_texture ? 1.0f : metallic.value_or(0.0f));
        add_texture("metallic_texture", kBindingMetallicTexture,
                    metallic_color_space.value_or(MaterialTextureColorSpace::Linear),
                    metallic_texture.value_or(white),
                    metallic_block_texture.value_or(asset::AssetID{}));
        add_scalar("roughness", roughness_texture ? 1.0f : roughness.value_or(1.0f));
        add_texture("roughness_texture", kBindingRoughnessTexture,
                    roughness_color_space.value_or(MaterialTextureColorSpace::Linear),
                    roughness_texture.value_or(white),
                    roughness_block_texture.value_or(asset::AssetID{}));
        add_scalar("occlusion", occlusion_texture ? 1.0f : occlusion.value_or(1.0f));
        add_texture("occlusion_texture", kBindingOcclusionTexture,
                    occlusion_color_space.value_or(MaterialTextureColorSpace::Linear),
                    occlusion_texture.value_or(white),
                    occlusion_block_texture.value_or(asset::AssetID{}));
        add_vector("emissive", emissive.value_or(Vector4f{0.f, 0.f, 0.f, 0.f}));
        add_vector("texture_channels",
                   Vector4f{metallic_channel, roughness_channel, occlusion_channel, 0.0f});
        add_scalar("normal_scale", normal_scale.value_or(1.0f));
        add_scalar("alpha_cutoff",
                   material.surface.alpha_mode == asset::MaterialAlphaMode::Mask
                       ? material.surface.alpha_cutoff
                       : 0.0f);

        template_desc.shading_model = MaterialShadingModel::StandardPbr;
        // PBR resolves bound samplers only (both APIs); bindless is a later
        // performance follow-up. ShadowDepth joins the pass list in D4.
        template_desc.bindless_texture_table_compatible = false;
        template_desc.compatible_passes = {MaterialPass::GBuffer};
        return true;
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
