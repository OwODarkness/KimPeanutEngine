#ifndef KPENGINE_RUNTIME_RESOURCE_ENVIRONMENT_IBL_PROCESSOR_H
#define KPENGINE_RUNTIME_RESOURCE_ENVIRONMENT_IBL_PROCESSOR_H

#include <cstdint>
#include <optional>

#include "data/texture.h"

namespace kpengine::resource
{
    struct EnvironmentIblSettings
    {
        uint32_t irradiance_width = 32;
        uint32_t irradiance_height = 16;
        uint32_t prefilter_width = 64;
        uint32_t prefilter_height = 32;
        uint32_t prefilter_level_count = 5;
        uint32_t brdf_lut_size = 64;
        uint32_t sample_count = 64;
    };

    struct EnvironmentIblData
    {
        data::TextureData irradiance;
        // Roughness levels are stored as equal-height vertical bands. This is
        // the common 2D representation until every RHI can upload cube mips.
        data::TextureData prefiltered_radiance;
        data::TextureData brdf_lut;
        uint32_t prefilter_level_count = 0;
    };

    std::optional<EnvironmentIblData> BuildEnvironmentIbl(
        const data::TextureData &source,
        const EnvironmentIblSettings &settings = {});
}

#endif
