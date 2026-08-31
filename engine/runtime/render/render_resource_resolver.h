#ifndef KPENGINE_RUNTIME_RENDER_RENDER_RESOURCE_RESOLVER_H
#define KPENGINE_RUNTIME_RENDER_RENDER_RESOURCE_RESOLVER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "asset/common.h"
#include "graphics/backend/common/api.h"
#include "graphics/backend/common/bindless_texture.h"
#include "graphics/backend/common/pipeline_types.h"
#include "graphics/backend/common/texture.h"
#include "pipeline_cache_key.h"
#include "render/material/material_system.h"
#include "render_resource.h"

namespace kpengine::asset
{
    struct ShaderProgramResource;
}

namespace kpengine::data
{
    struct MeshData;
    struct TextureData;
}

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::resource
{
    class ResourcePipeline;
}

namespace kpengine::render
{
    enum class TextureCacheVariant : uint8_t
    {
        Source,
        EnvironmentIrradiance,
        EnvironmentPrefilter,
        EnvironmentBrdfLut,
    };

    // Texture identity includes source asset generation, resolved GPU format,
    // and a Render-private derived-artifact variant. This separates sRGB,
    // linear UNORM, HDR, and environment-derived interpretations without
    // folding policy bits into AssetID generation bits.
    struct TextureCacheKey
    {
        asset::AssetID asset_id{};
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        TextureCacheVariant variant = TextureCacheVariant::Source;

        bool operator==(const TextureCacheKey &other) const noexcept
        {
            return asset_id == other.asset_id && format == other.format &&
                   variant == other.variant;
        }
    };

    struct TextureCacheKeyHash
    {
        std::size_t operator()(const TextureCacheKey &key) const noexcept
        {
            const std::size_t asset_hash = std::hash<uint64_t>{}(key.asset_id.Pack());
            const std::size_t format_hash = std::hash<uint8_t>{}(
                static_cast<uint8_t>(key.format));
            const std::size_t variant_hash = std::hash<uint8_t>{}(
                static_cast<uint8_t>(key.variant));
            return asset_hash ^ (format_hash << 1) ^ (variant_hash << 3);
        }
    };

    // Render-private owner for resolved static RHI resources. It accepts only
    // render policy and asset-backed CPU data; it never exposes backend-native
    // objects or lets callers access the backend directly.
    class RenderResourceResolver final : public IMaterialResourceResolver
    {
    public:
        struct ResolvedMaterialTextureBindings
        {
            std::unordered_map<uint32_t, TextureBinding> textures;
            std::unordered_map<uint32_t, graphics::BindlessTextureHandle> bindless_slots;
            bool uses_bindless_textures = false;
        };
        RenderResourceResolver(graphics::RenderBackend &backend,
                               resource::ResourcePipeline &resource_pipeline);

        graphics::PipelineHandle GetOrCreateDefaultPipeline(
            asset::AssetID program_id, asset::ShaderProgramResource &program,
            const MaterialPipelineState *material_state = nullptr,
            bool bindless_texture_table_compatible = false,
            MaterialPass pass = MaterialPass::Scene);
        graphics::MeshHandle GetOrCreateMesh(asset::AssetID asset_id,
                                             const data::MeshData &data);
        TextureBinding GetOrCreateTextureBinding(
            asset::AssetID asset_id, const data::TextureData &data,
            MaterialTextureColorSpace color_space = MaterialTextureColorSpace::Srgb,
            const MaterialSamplerDesc *sampler_desc = nullptr,
            TextureCacheVariant variant = TextureCacheVariant::Source);
        MaterialResolution ResolveTemplate(MaterialTemplateHandle handle,
                                           const MaterialTemplateDesc &desc) override;
        MaterialResolution ResolveInstance(MaterialInstanceHandle handle,
            const MaterialTemplateDesc &desc,
            const std::vector<MaterialParameterValue> &effective_values) override;
        void ReleaseTemplate(MaterialTemplateHandle handle) override;
        void ReleaseInstance(MaterialInstanceHandle handle) override;
        graphics::PipelineHandle FindMaterialPipeline(MaterialTemplateHandle handle,
                                                      MaterialPass pass) const;
        const ResolvedMaterialTextureBindings *FindTextureBindings(
            MaterialInstanceHandle handle) const;
        bool UsesBindlessTextures(MaterialInstanceHandle handle) const;
        void Cleanup();

    private:
        static bool BuildDefaultPipelineDesc(asset::ShaderProgramResource &program,
                                             graphics::PipelineDesc &out_desc,
                                             const MaterialPipelineState *material_state,
                                             bool bindless_texture_table_compatible,
                                             MaterialPass pass);
        static graphics::TextureSettings DefaultTextureSettings();
        graphics::SamplerHandle GetOrCreateDefaultSampler();
        graphics::SamplerHandle GetOrCreateSampler(const MaterialSamplerDesc &desc);

        graphics::RenderBackend *backend_ = nullptr;
        resource::ResourcePipeline *resource_pipeline_ = nullptr;
        std::unordered_map<PipelineCacheKey, graphics::PipelineHandle, PipelineCacheKeyHash>
            pipeline_cache_;
        std::unordered_map<uint64_t, graphics::MeshHandle> mesh_cache_;
        std::unordered_map<TextureCacheKey, graphics::TextureHandle, TextureCacheKeyHash>
            texture_cache_;
        std::unordered_map<uint64_t, graphics::SamplerHandle> material_sampler_cache_;
        std::unordered_map<MaterialTemplateHandle,
                           std::unordered_map<MaterialPass, graphics::PipelineHandle>>
            material_pipelines_;
        std::unordered_map<MaterialInstanceHandle, ResolvedMaterialTextureBindings>
            material_texture_bindings_;
        graphics::SamplerHandle default_sampler_handle_;
    };
}

#endif
