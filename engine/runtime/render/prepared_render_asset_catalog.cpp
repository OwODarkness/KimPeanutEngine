#include "prepared_render_asset_catalog.h"

#include <algorithm>
#include <type_traits>

#include "asset/shader.h"
#include "asset/shader_program.h"

namespace kpengine::render
{
    namespace
    {
        constexpr std::array<BuiltInRenderAssetRequirement,
                             static_cast<size_t>(BuiltInRenderAsset::Count)>
            kBuiltInRequirements{{
                {BuiltInRenderAsset::DeferredLightingProgram, "shader/deferred_lighting.shader",
                 asset::AssetType::KPAT_ShaderProgram},
                {BuiltInRenderAsset::GBufferDebugProgram, "shader/gbuffer_debug_view.shader",
                 asset::AssetType::KPAT_ShaderProgram},
                {BuiltInRenderAsset::ToneMapProgram, "shader/tone_map.shader",
                 asset::AssetType::KPAT_ShaderProgram},
                {BuiltInRenderAsset::CaptureViewProgram, "shader/capture_view.shader",
                 asset::AssetType::KPAT_ShaderProgram},
                {BuiltInRenderAsset::DirectionalShadowProgram,
                 "shader/directional_shadow_depth.shader", asset::AssetType::KPAT_ShaderProgram},
                {BuiltInRenderAsset::DefaultWhiteTexture, "texture/default/default_white.png",
                 asset::AssetType::KPAT_Texture},
                {BuiltInRenderAsset::DefaultFlatNormalTexture,
                 "texture/default/default_flat_normal.png", asset::AssetType::KPAT_Texture},
            }};

        bool PayloadMatchesType(const PreparedRenderPayload &payload, asset::AssetType type)
        {
            return std::visit(
                [type](const auto &ptr)
                {
                    using Pointer = std::decay_t<decltype(ptr)>;
                    if (ptr == nullptr)
                    {
                        return false;
                    }
                    if constexpr (std::is_same_v<Pointer, asset::MeshPtr>)
                    {
                        return type == asset::AssetType::KPAT_Mesh;
                    }
                    if constexpr (std::is_same_v<Pointer, asset::TexturePtr>)
                    {
                        return type == asset::AssetType::KPAT_Texture;
                    }
                    if constexpr (std::is_same_v<Pointer, asset::ShaderPtr>)
                    {
                        return type == asset::AssetType::KPAT_Shader;
                    }
                    if constexpr (std::is_same_v<Pointer, asset::ShaderProgramPtr>)
                    {
                        return type == asset::AssetType::KPAT_ShaderProgram;
                    }
                    if constexpr (std::is_same_v<Pointer, asset::MaterialPtr>)
                    {
                        return type == asset::AssetType::KPAT_Material;
                    }
                    return false;
                },
                payload);
        }

        bool IsTextureDataReady(const data::TextureData &data)
        {
            return data.width != 0 && data.height != 0 && !data.pixels.empty();
        }

        bool IsSupportedGraphicsApi(GraphicsAPIType api)
        {
            return api == GraphicsAPIType::GRAPHICS_API_VULKAN ||
                   api == GraphicsAPIType::GRAPHICS_API_OPENGL;
        }

        bool IsShaderArtifactReady(const asset::ShaderResource &shader,
                                   GraphicsAPIType api)
        {
            if (shader.status != asset::ShaderStatus::Ready || !shader.data ||
                shader.data->api != api || shader.desc.stage == ShaderStage::SHADER_STAGE_UNKNOW ||
                shader.data->stage != shader.desc.stage)
            {
                return false;
            }
            return api == GraphicsAPIType::GRAPHICS_API_VULKAN
                       ? !shader.data->byte_code.empty()
                       : !shader.data->source.empty();
        }

        bool ContainsDependency(const PreparedRenderAssetRecord &record,
                                asset::AssetID dependency)
        {
            return std::find(record.dependencies.begin(), record.dependencies.end(), dependency) !=
                   record.dependencies.end();
        }

        bool ValidateProgram(const PreparedRenderAssetRecord &record,
                             const std::unordered_map<uint64_t, PreparedRenderAssetRecord> &records,
                             std::string &diagnostic)
        {
            const auto *program = std::get_if<asset::ShaderProgramPtr>(&record.payload);
            if (program == nullptr || *program == nullptr)
            {
                return false;
            }

            for (const ShaderStage stage : {ShaderStage::SHADER_STAGE_VERTEX,
                                            ShaderStage::SHADER_STAGE_FRAGMENT})
            {
                const asset::AssetID shader_id = (*program)->GetData(
                    stage, ShaderFormat::SHADER_FORMAT_GLSL,
                    asset::ShaderProgramVariant::Bound);
                const auto shader_record = records.find(shader_id.Pack());
                if (!shader_id.IsValid() || shader_id.type != asset::AssetType::KPAT_Shader ||
                    shader_record == records.end() || !(shader_record->second.id == shader_id) ||
                    !ContainsDependency(record, shader_id))
                {
                    diagnostic = "prepared shader program AssetID " +
                                 std::to_string(record.id.Pack()) +
                                 " has an unbound or unlisted required stage";
                    return false;
                }

                const auto shader = std::get_if<asset::ShaderPtr>(&shader_record->second.payload);
                if (shader == nullptr || *shader == nullptr || (*shader)->format != ShaderFormat::SHADER_FORMAT_GLSL ||
                    (*shader)->variant != asset::ShaderProgramVariant::Bound ||
                    (*shader)->desc.stage != stage || (*shader)->data == nullptr ||
                    (*shader)->data->stage != stage)
                {
                    diagnostic = "prepared shader program AssetID " +
                                 std::to_string(record.id.Pack()) +
                                 " has a stage binding that disagrees with its dependency";
                    return false;
                }
            }
            return true;
        }
    }

    const std::array<BuiltInRenderAssetRequirement,
                     static_cast<size_t>(BuiltInRenderAsset::Count)> &
    GetBuiltInRenderAssetRequirements()
    {
        return kBuiltInRequirements;
    }

    std::optional<PreparedRenderAssetCatalog> PreparedRenderAssetCatalog::Create(
        PreparedRenderAssetCatalogBuild build, std::string &diagnostic)
    {
        if (!IsSupportedGraphicsApi(build.graphics_api))
        {
            diagnostic = "prepared render catalog has no supported graphics API";
            return std::nullopt;
        }

        PreparedRenderAssetCatalog catalog;
        catalog.prepared_shader_count_ = build.prepared_shader_count;

        for (auto &record : build.records)
        {
            if (!record.id.IsValid() || !PayloadMatchesType(record.payload, record.id.type))
            {
                diagnostic = "prepared render catalog contains an invalid payload for AssetID " +
                             std::to_string(record.id.Pack());
                return std::nullopt;
            }
            if (!catalog.records_.emplace(record.id.Pack(), std::move(record)).second)
            {
                diagnostic = "prepared render catalog contains a duplicate AssetID";
                return std::nullopt;
            }
        }

        for (const auto &[packed_id, record] : catalog.records_)
        {
            (void)packed_id;
            if (record.id.type == asset::AssetType::KPAT_Shader)
            {
                const auto *shader = std::get_if<asset::ShaderPtr>(&record.payload);
                if (shader == nullptr || *shader == nullptr ||
                    !IsShaderArtifactReady(**shader, build.graphics_api))
                {
                    diagnostic = "prepared shader AssetID " + std::to_string(record.id.Pack()) +
                                 " is not ready for the selected graphics API";
                    return std::nullopt;
                }
            }
            for (const asset::AssetID dependency : record.dependencies)
            {
                const auto dependency_record = catalog.records_.find(dependency.Pack());
                if (!dependency.IsValid() || dependency_record == catalog.records_.end() ||
                    !(dependency_record->second.id == dependency))
                {
                    diagnostic = "prepared render catalog has an unresolved dependency for AssetID " +
                                 std::to_string(record.id.Pack());
                    return std::nullopt;
                }
            }
        }

        for (const auto &[packed_id, record] : catalog.records_)
        {
            (void)packed_id;
            if (record.id.type == asset::AssetType::KPAT_ShaderProgram &&
                !ValidateProgram(record, catalog.records_, diagnostic))
            {
                if (diagnostic.empty())
                {
                    diagnostic = "prepared shader program has an invalid payload";
                }
                return std::nullopt;
            }
        }

        for (size_t index = 0; index < build.built_ins.size(); ++index)
        {
            const asset::AssetID id = build.built_ins[index];
            const auto &requirement = kBuiltInRequirements[index];
            if (requirement.role != static_cast<BuiltInRenderAsset>(index))
            {
                diagnostic = "built-in Render asset role table is not ordinally aligned";
                return std::nullopt;
            }
            const auto record = catalog.records_.find(id.Pack());
            if (!id.IsValid() || id.type != requirement.expected_type ||
                record == catalog.records_.end() || !(record->second.id == id))
            {
                diagnostic = "prepared render catalog is missing built-in role " +
                             std::to_string(index);
                return std::nullopt;
            }
            catalog.built_ins_[index] = id;
        }

        for (auto &environment : build.environment_ibl)
        {
            const auto source = catalog.records_.find(environment.source_texture.Pack());
            if (!environment.source_texture.IsValid() ||
                environment.source_texture.type != asset::AssetType::KPAT_Texture ||
                source == catalog.records_.end() ||
                !std::holds_alternative<asset::TexturePtr>(source->second.payload) ||
                !IsTextureDataReady(environment.data.irradiance) ||
                !IsTextureDataReady(environment.data.prefiltered_radiance) ||
                !IsTextureDataReady(environment.data.brdf_lut) ||
                environment.data.prefilter_level_count == 0 ||
                !catalog.environment_ibl_.emplace(environment.source_texture.Pack(),
                                                  std::move(environment)).second)
            {
                diagnostic = "prepared render catalog contains invalid environment IBL data";
                return std::nullopt;
            }
        }
        diagnostic.clear();
        return catalog;
    }

    asset::AssetID PreparedRenderAssetCatalog::GetBuiltIn(BuiltInRenderAsset role) const noexcept
    {
        const size_t index = static_cast<size_t>(role);
        return index < built_ins_.size() ? built_ins_[index] : asset::AssetID{};
    }

    asset::AssetID PreparedRenderAssetCatalog::ResolveDependency(
        asset::AssetID owner, size_t index, asset::AssetType expected_type) const noexcept
    {
        const auto record = records_.find(owner.Pack());
        if (record == records_.end() || !(record->second.id == owner) ||
            index >= record->second.dependencies.size())
        {
            return {};
        }
        const asset::AssetID dependency = record->second.dependencies[index];
        return dependency.type == expected_type && records_.find(dependency.Pack()) != records_.end()
                   ? dependency
                   : asset::AssetID{};
    }

    const PreparedEnvironmentIbl *PreparedRenderAssetCatalog::FindEnvironmentIbl(
        asset::AssetID source_texture) const
    {
        const auto found = environment_ibl_.find(source_texture.Pack());
        return found == environment_ibl_.end() ? nullptr : &found->second;
    }
}
