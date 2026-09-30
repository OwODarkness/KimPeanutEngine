#include "environment_bindings_owner.h"

#include "asset/texture.h"
#include "data/texture.h"
#include "log/logger.h"
#include "render/material/material_asset_resolver.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/render_resource_resolver.h"

namespace kpengine::render
{
    void EnvironmentBindingsOwner::Update(
        const std::optional<EnvironmentSourceDesc> &source,
        const std::optional<EnvironmentSourceHandle> &source_handle,
        RenderResourceResolver &resolver,
        const PreparedRenderAssetCatalog &prepared_assets)
    {
        if (!source.has_value() || !source_handle.has_value())
        {
            failed_source_.reset();
            active_ = {};
            return;
        }
        if (level_.source_asset == source->texture_asset && level_.HasCompleteBindings())
        {
            level_.ibl_intensity = source->ibl_intensity;
            active_ = level_;
            failed_source_.reset();
            return;
        }

        EnvironmentFrameBindings candidate{};
        if (ResolveLevelEnvironment(*source, resolver, prepared_assets, candidate))
        {
            level_ = candidate;
            active_ = std::move(candidate);
            failed_source_.reset();
            return;
        }
        if (!failed_source_.has_value() || !(*failed_source_ == *source_handle))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Level environment source could not be resolved; retaining baseline");
            failed_source_ = source_handle;
        }
        active_ = {};
    }

    bool EnvironmentBindingsOwner::EnsureFallback(RenderResourceResolver &resolver)
    {
        if (active_.HasCompleteBindings())
            return true;

        data::TextureData fallback{};
        fallback.width = 1;
        fallback.height = 1;
        fallback.format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;
        fallback.pixels = {0, 0, 0, 0xff};

        EnvironmentFrameBindings black_fallback{};
        black_fallback.ibl_intensity = 0.25f;
        black_fallback.panorama = resolver.GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear);
        black_fallback.irradiance = resolver.GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
            TextureCacheVariant::EnvironmentIrradiance);
        black_fallback.prefiltered_radiance = resolver.GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
            TextureCacheVariant::EnvironmentPrefilter);
        black_fallback.brdf_lut = resolver.GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
            TextureCacheVariant::EnvironmentBrdfLut);
        black_fallback.ibl_enabled = false;
        if (!black_fallback.HasCompleteBindings())
            return false;
        active_ = std::move(black_fallback);
        return true;
    }

    void EnvironmentBindingsOwner::Clear() noexcept
    {
        level_ = {};
        active_ = {};
        failed_source_.reset();
    }

    bool EnvironmentBindingsOwner::ResolveLevelEnvironment(
        const EnvironmentSourceDesc &source, RenderResourceResolver &resolver,
        const PreparedRenderAssetCatalog &prepared_assets, EnvironmentFrameBindings &bundle)
    {
        if (!IsEnvironmentSourceDescValid(source))
            return false;
        const auto texture_resource =
            prepared_assets.Get<asset::TextureResource>(source.texture_asset);
        if (!texture_resource || !texture_resource->data)
            return false;

        MaterialSamplerDesc panorama_sampler{};
        panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        bundle = {};
        bundle.source_asset = source.texture_asset;
        bundle.ibl_intensity = source.ibl_intensity;
        bundle.panorama = resolver.GetOrCreateTextureBinding(
            source.texture_asset, *texture_resource->data,
            MaterialTextureColorSpace::Srgb, &panorama_sampler);
        if (!bundle.panorama.texture.IsValid() || !bundle.panorama.sampler.IsValid())
            return false;
        return PrepareEnvironmentIbl(source.texture_asset, resolver, prepared_assets, bundle);
    }

    bool EnvironmentBindingsOwner::PrepareEnvironmentIbl(
        asset::AssetID source_asset, RenderResourceResolver &resolver,
        const PreparedRenderAssetCatalog &prepared_assets,
        EnvironmentFrameBindings &bundle)
    {
        const PreparedEnvironmentIbl *const prepared =
            prepared_assets.FindEnvironmentIbl(source_asset);
        if (prepared == nullptr)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Environment IBL preprocessing requires a valid RGBA16F panorama");
            return false;
        }

        MaterialSamplerDesc panorama_sampler{};
        panorama_sampler.address_u = MaterialSamplerAddressMode::Repeat;
        panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        bundle.irradiance = resolver.GetOrCreateTextureBinding(
            source_asset, prepared->data.irradiance, MaterialTextureColorSpace::Linear,
            &panorama_sampler, TextureCacheVariant::EnvironmentIrradiance);
        bundle.prefiltered_radiance = resolver.GetOrCreateTextureBinding(
            source_asset, prepared->data.prefiltered_radiance,
            MaterialTextureColorSpace::Linear, &panorama_sampler,
            TextureCacheVariant::EnvironmentPrefilter);

        MaterialSamplerDesc lut_sampler{};
        lut_sampler.address_u = MaterialSamplerAddressMode::ClampToEdge;
        lut_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        lut_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        bundle.brdf_lut = resolver.GetOrCreateTextureBinding(
            source_asset, prepared->data.brdf_lut, MaterialTextureColorSpace::Linear,
            &lut_sampler, TextureCacheVariant::EnvironmentBrdfLut);
        bundle.prefilter_level_count = prepared->data.prefilter_level_count;
        bundle.ibl_enabled = bundle.HasCompleteBindings();
        return bundle.ibl_enabled;
    }
}
