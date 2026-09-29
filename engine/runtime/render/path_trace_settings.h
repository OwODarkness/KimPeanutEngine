#ifndef KPENGINE_RUNTIME_RENDER_PATH_TRACE_SETTINGS_H
#define KPENGINE_RUNTIME_RENDER_PATH_TRACE_SETTINGS_H

#include <cstdint>
#include <cmath>
#include <optional>
#include <string_view>

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

    enum class PathTraceDirectLightSampling : uint8_t
    {
        AllLights,
        UniformOneLight,
    };

    enum class PathTraceSamplingPolicy : uint8_t
    {
        Fixed,
        AdaptiveCameraMotion,
    };

    struct PathTraceSettings
    {
        bool path_tracing_enabled = true;
        bool hybrid_ray_query_shadows_enabled = true;
        PathTraceVisibilityMethod visibility_method = PathTraceVisibilityMethod::RayPipeline;
        uint32_t samples_per_dispatch = 4;
        uint32_t maximum_continuation_bounces = 8;
        PathTraceReconstruction reconstruction = PathTraceReconstruction::Raw;
        PathTraceReconstruction adaptive_moving_reconstruction =
            PathTraceReconstruction::GuidedPreview;
        PathTraceDirectLightSampling direct_light_sampling =
            PathTraceDirectLightSampling::AllLights;
        PathTraceSamplingPolicy sampling_policy =
            PathTraceSamplingPolicy::AdaptiveCameraMotion;
        uint32_t moving_samples_per_dispatch = 1;
        uint32_t settled_samples_per_dispatch = 4;
        uint32_t quality_2spp_samples_per_dispatch = 2;
        uint32_t quality_2spp_sample_threshold = 100;
        uint32_t quality_1spp_sample_threshold = 200;
        uint32_t quality_maintenance_samples_per_dispatch = 1;
        uint32_t settle_frame_threshold = 8;
        float camera_translation_threshold = 0.02f;
        float camera_rotation_threshold_degrees = 0.2f;
        PathTraceOutputProbe output_probe = PathTraceOutputProbe::Beauty;
        PathTraceTexturePolicy texture_policy = PathTraceTexturePolicy::AuthoredMaterials;
    };

    inline constexpr std::optional<PathTraceVisibilityMethod> ParsePathTraceVisibilityMethod(
        std::string_view value) noexcept
    {
        if (value == "ray_pipeline") return PathTraceVisibilityMethod::RayPipeline;
        if (value == "ray_query") return PathTraceVisibilityMethod::RayQuery;
        return std::nullopt;
    }

    inline constexpr std::optional<PathTraceReconstruction> ParsePathTraceReconstruction(
        std::string_view value) noexcept
    {
        if (value == "raw") return PathTraceReconstruction::Raw;
        if (value == "guided_preview") return PathTraceReconstruction::GuidedPreview;
        if (value == "variance_denoise") return PathTraceReconstruction::VarianceDenoise;
        return std::nullopt;
    }

    inline constexpr std::optional<PathTraceDirectLightSampling>
    ParsePathTraceDirectLightSampling(std::string_view value) noexcept
    {
        if (value == "all_lights") return PathTraceDirectLightSampling::AllLights;
        if (value == "uniform_one_light")
            return PathTraceDirectLightSampling::UniformOneLight;
        return std::nullopt;
    }

    inline constexpr std::optional<PathTraceSamplingPolicy> ParsePathTraceSamplingPolicy(
        std::string_view value) noexcept
    {
        if (value == "fixed") return PathTraceSamplingPolicy::Fixed;
        if (value == "adaptive_camera_motion")
            return PathTraceSamplingPolicy::AdaptiveCameraMotion;
        return std::nullopt;
    }

    inline constexpr std::optional<PathTraceOutputProbe> ParsePathTraceOutputProbe(
        std::string_view value) noexcept
    {
        if (value == "beauty") return PathTraceOutputProbe::Beauty;
        if (value == "primary_visibility") return PathTraceOutputProbe::PrimaryVisibility;
        if (value == "primary_normal") return PathTraceOutputProbe::PrimaryNormal;
        if (value == "primary_albedo") return PathTraceOutputProbe::PrimaryAlbedo;
        if (value == "direct_only") return PathTraceOutputProbe::DirectOnly;
        if (value == "surface_parameters") return PathTraceOutputProbe::SurfaceParameters;
        if (value == "ray_cone_filtering") return PathTraceOutputProbe::RayConeFiltering;
        return std::nullopt;
    }

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
               (settings.adaptive_moving_reconstruction == PathTraceReconstruction::Raw ||
                settings.adaptive_moving_reconstruction ==
                    PathTraceReconstruction::GuidedPreview ||
                settings.adaptive_moving_reconstruction ==
                    PathTraceReconstruction::VarianceDenoise) &&
               (settings.direct_light_sampling == PathTraceDirectLightSampling::AllLights ||
                settings.direct_light_sampling == PathTraceDirectLightSampling::UniformOneLight) &&
               (settings.sampling_policy == PathTraceSamplingPolicy::Fixed ||
                settings.sampling_policy == PathTraceSamplingPolicy::AdaptiveCameraMotion) &&
               settings.moving_samples_per_dispatch >= 1 &&
               settings.moving_samples_per_dispatch <= 16 &&
               settings.settled_samples_per_dispatch >= 1 &&
               settings.settled_samples_per_dispatch <= 16 &&
               settings.quality_2spp_sample_threshold >= 1 &&
               settings.quality_2spp_sample_threshold <
                   settings.quality_1spp_sample_threshold &&
               settings.quality_1spp_sample_threshold <= 1000000 &&
               settings.quality_2spp_samples_per_dispatch >= 1 &&
               settings.quality_2spp_samples_per_dispatch <= 16 &&
               settings.quality_maintenance_samples_per_dispatch >= 1 &&
               settings.quality_maintenance_samples_per_dispatch <= 16 &&
               settings.settle_frame_threshold >= 1 &&
               settings.settle_frame_threshold <= 120 &&
               std::isfinite(settings.camera_translation_threshold) &&
               settings.camera_translation_threshold > 0.0f &&
               settings.camera_translation_threshold <= 10.0f &&
               std::isfinite(settings.camera_rotation_threshold_degrees) &&
               settings.camera_rotation_threshold_degrees > 0.0f &&
               settings.camera_rotation_threshold_degrees <= 180.0f &&
               valid_output &&
               settings.texture_policy == PathTraceTexturePolicy::AuthoredMaterials;
    }

    inline constexpr uint32_t PackPathTraceShaderMode(
        const PathTraceSettings &settings) noexcept
    {
        constexpr uint32_t kQueryVisibilityBit = 1u << 8u;
        constexpr uint32_t kDenoiseBit = 1u << 9u;
        constexpr uint32_t kGuidedPreviewBit = 1u << 10u;
        constexpr uint32_t kUniformOneLightBit = 1u << 11u;
        constexpr uint32_t kFrameSequenceBit = 1u << 12u;
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
        if (settings.direct_light_sampling ==
            PathTraceDirectLightSampling::UniformOneLight)
        {
            packed |= kUniformOneLightBit;
        }
        if (settings.sampling_policy == PathTraceSamplingPolicy::AdaptiveCameraMotion)
        {
            packed |= kFrameSequenceBit;
        }
        return packed;
    }

    inline constexpr uint32_t PackPathTraceHistoryMode(
        const PathTraceSettings &settings) noexcept
    {
        // Reconstruction runs after radiance accumulation.
        return PackPathTraceShaderMode(settings) & ~((1u << 9u) | (1u << 10u));
    }

    inline constexpr PathTraceSettings ApplyLegacyPathTraceProbeMode(
        PathTraceSettings settings, PathTraceProbeMode mode) noexcept
    {
        if (mode == PathTraceProbeMode::SingleLightPreview)
        {
            settings.samples_per_dispatch = 1;
            settings.reconstruction = PathTraceReconstruction::GuidedPreview;
            settings.direct_light_sampling =
                PathTraceDirectLightSampling::UniformOneLight;
            settings.output_probe = PathTraceOutputProbe::Beauty;
            return settings;
        }

        settings.visibility_method = PathTraceVisibilityMethod::RayPipeline;
        settings.samples_per_dispatch = 4;
        settings.reconstruction = PathTraceReconstruction::Raw;
        settings.direct_light_sampling = PathTraceDirectLightSampling::AllLights;
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
