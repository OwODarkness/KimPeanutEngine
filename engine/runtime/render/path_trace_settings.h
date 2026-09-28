#ifndef KPENGINE_RUNTIME_RENDER_PATH_TRACE_SETTINGS_H
#define KPENGINE_RUNTIME_RENDER_PATH_TRACE_SETTINGS_H

#include <cstdint>

#include "path_trace_probe_mode.h"

namespace kpengine::render
{
    enum class PathTraceVisibilityMethod : uint8_t
    {
        RayPipeline,
        RayQuery,
    };

    enum class PathTraceReconstruction : uint8_t
    {
        Raw,
        GuidedPreview,
        VarianceDenoise,
    };

    enum class PathTraceOutputProbe : uint8_t
    {
        Beauty = 0,
        PrimaryVisibility = 1,
        PrimaryNormal = 2,
        PrimaryAlbedo = 3,
        DirectOnly = 4,
        SurfaceParameters = 5,
        RayConeFiltering = 7,
    };

    enum class PathTraceTexturePolicy : uint8_t
    {
        AuthoredMaterials,
    };

    struct PathTraceSettings
    {
        bool path_tracing_enabled = true;
        bool hybrid_ray_query_shadows_enabled = true;
        PathTraceVisibilityMethod visibility_method = PathTraceVisibilityMethod::RayPipeline;
        uint32_t samples_per_dispatch = 4;
        uint32_t maximum_continuation_bounces = 8;
        PathTraceReconstruction reconstruction = PathTraceReconstruction::Raw;
        PathTraceOutputProbe output_probe = PathTraceOutputProbe::Beauty;
        PathTraceTexturePolicy texture_policy = PathTraceTexturePolicy::AuthoredMaterials;
    };

    inline constexpr bool IsValidPathTraceSettings(const PathTraceSettings &settings) noexcept
    {
        const bool valid_output =
            settings.output_probe == PathTraceOutputProbe::Beauty ||
            settings.output_probe == PathTraceOutputProbe::PrimaryVisibility ||
            settings.output_probe == PathTraceOutputProbe::PrimaryNormal ||
            settings.output_probe == PathTraceOutputProbe::PrimaryAlbedo ||
            settings.output_probe == PathTraceOutputProbe::DirectOnly ||
            settings.output_probe == PathTraceOutputProbe::SurfaceParameters ||
            settings.output_probe == PathTraceOutputProbe::RayConeFiltering;
        return settings.samples_per_dispatch >= 1 && settings.samples_per_dispatch <= 16 &&
               settings.maximum_continuation_bounces <= 32 &&
               (settings.visibility_method == PathTraceVisibilityMethod::RayPipeline ||
                settings.visibility_method == PathTraceVisibilityMethod::RayQuery) &&
               (settings.reconstruction == PathTraceReconstruction::Raw ||
                settings.reconstruction == PathTraceReconstruction::GuidedPreview ||
                settings.reconstruction == PathTraceReconstruction::VarianceDenoise) &&
               valid_output &&
               settings.texture_policy == PathTraceTexturePolicy::AuthoredMaterials;
    }

    inline constexpr uint32_t PackPathTraceShaderMode(
        const PathTraceSettings &settings) noexcept
    {
        constexpr uint32_t kQueryVisibilityBit = 1u << 8u;
        constexpr uint32_t kDenoiseBit = 1u << 9u;
        constexpr uint32_t kGuidedPreviewBit = 1u << 10u;
        uint32_t packed = static_cast<uint32_t>(settings.output_probe);
        if (settings.visibility_method == PathTraceVisibilityMethod::RayQuery)
        {
            packed |= kQueryVisibilityBit;
        }
        if (settings.reconstruction == PathTraceReconstruction::VarianceDenoise)
        {
            packed |= kDenoiseBit;
        }
        else if (settings.reconstruction == PathTraceReconstruction::GuidedPreview)
        {
            packed |= kGuidedPreviewBit;
        }
        return packed;
    }

    inline constexpr PathTraceSettings ApplyLegacyPathTraceProbeMode(
        PathTraceSettings settings, PathTraceProbeMode mode) noexcept
    {
        settings.visibility_method = PathTraceVisibilityMethod::RayPipeline;
        settings.samples_per_dispatch = 4;
        settings.reconstruction = PathTraceReconstruction::Raw;
        switch (mode)
        {
        case PathTraceProbeMode::PrimaryVisibility:
            settings.output_probe = PathTraceOutputProbe::PrimaryVisibility;
            break;
        case PathTraceProbeMode::PrimaryNormal:
            settings.output_probe = PathTraceOutputProbe::PrimaryNormal;
            break;
        case PathTraceProbeMode::PrimaryAlbedo:
            settings.output_probe = PathTraceOutputProbe::PrimaryAlbedo;
            break;
        case PathTraceProbeMode::DirectOnly:
            settings.output_probe = PathTraceOutputProbe::DirectOnly;
            break;
        case PathTraceProbeMode::SurfaceParameters:
            settings.output_probe = PathTraceOutputProbe::SurfaceParameters;
            break;
        case PathTraceProbeMode::RayQueryVisibility:
            settings.output_probe = PathTraceOutputProbe::Beauty;
            settings.visibility_method = PathTraceVisibilityMethod::RayQuery;
            break;
        case PathTraceProbeMode::RayConeFiltering:
            settings.output_probe = PathTraceOutputProbe::RayConeFiltering;
            break;
        case PathTraceProbeMode::LowSppPreview:
            settings.samples_per_dispatch = 1;
            settings.reconstruction = PathTraceReconstruction::GuidedPreview;
            break;
        case PathTraceProbeMode::BeautyDenoise:
            settings.reconstruction = PathTraceReconstruction::VarianceDenoise;
            break;
        case PathTraceProbeMode::Beauty:
        default:
            settings.output_probe = PathTraceOutputProbe::Beauty;
            break;
        }
        return settings;
    }
}

#endif
