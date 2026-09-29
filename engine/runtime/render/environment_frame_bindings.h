#ifndef KPENGINE_RUNTIME_RENDER_ENVIRONMENT_FRAME_BINDINGS_H
#define KPENGINE_RUNTIME_RENDER_ENVIRONMENT_FRAME_BINDINGS_H

#include <cstdint>

#include "asset/common.h"
#include "render_resource.h"

namespace kpengine::render
{
    // Borrowed environment textures shared by deferred lighting and path tracing.
    struct EnvironmentFrameBindings
    {
        asset::AssetID source_asset;
        TextureBinding panorama;
        TextureBinding irradiance;
        TextureBinding prefiltered_radiance;
        TextureBinding brdf_lut;
        uint32_t prefilter_level_count = 0;
        float ibl_intensity = 0.25f;
        bool ibl_enabled = false;

        bool HasCompleteBindings() const
        {
            return panorama.texture.IsValid() && panorama.sampler.IsValid() &&
                   irradiance.texture.IsValid() && irradiance.sampler.IsValid() &&
                   prefiltered_radiance.texture.IsValid() &&
                   prefiltered_radiance.sampler.IsValid() && brdf_lut.texture.IsValid() &&
                   brdf_lut.sampler.IsValid();
        }
    };
}

#endif
