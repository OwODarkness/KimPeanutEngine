#include "render_settings.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "render/render_system.h"

namespace kpengine::render
{
    namespace
    {
        using namespace settings;
        using reflection::ReflectionPropertyMetadata;
        using reflection::ReflectionValue;
        using reflection::ReflectionValueType;
        using reflection::ReflectionWidgetSemantic;

        SettingPropertyDefinition BoolProperty(
            std::string key, std::string label, bool default_value)
        {
            ReflectionPropertyMetadata metadata{};
            metadata.display_name = std::move(label);
            metadata.category = "Rendering";
            return {std::move(key), ReflectionValueType::Bool,
                    ReflectionValue{default_value}, std::move(metadata)};
        }

        SettingPropertyDefinition UnsignedProperty(
            std::string key, std::string label, uint64_t default_value,
            double minimum, double maximum, double step)
        {
            ReflectionPropertyMetadata metadata{};
            metadata.display_name = std::move(label);
            metadata.category = "Path tracing";
            metadata.minimum = minimum;
            metadata.maximum = maximum;
            metadata.step = step;
            return {std::move(key), ReflectionValueType::UnsignedInteger,
                    ReflectionValue{default_value}, std::move(metadata)};
        }

        SettingPropertyDefinition FloatProperty(
            std::string key, std::string label, double default_value,
            double minimum, double maximum, double step)
        {
            ReflectionPropertyMetadata metadata{};
            metadata.display_name = std::move(label);
            metadata.category = "Rendering";
            metadata.minimum = minimum;
            metadata.maximum = maximum;
            metadata.step = step;
            return {std::move(key), ReflectionValueType::FloatingPoint,
                    ReflectionValue{default_value}, std::move(metadata)};
        }

        SettingPropertyDefinition EnumProperty(
            std::string key, std::string label, int64_t default_value,
            std::vector<reflection::ReflectionEnumOption> options)
        {
            ReflectionPropertyMetadata metadata{};
            metadata.display_name = std::move(label);
            metadata.category = "Path tracing";
            metadata.semantic = ReflectionWidgetSemantic::Enum;
            metadata.enum_options = std::move(options);
            return {std::move(key), ReflectionValueType::SignedInteger,
                    ReflectionValue{default_value}, std::move(metadata)};
        }

        template <typename T>
        std::optional<T> Read(const SettingValues &values, const char *key)
        {
            const auto found = values.find(key);
            if (found == values.end())
            {
                return std::nullopt;
            }
            if (const T *value = found->second.TryGet<T>())
            {
                return *value;
            }
            return std::nullopt;
        }

        std::optional<PathTraceSettings> ConvertPathTraceSettings(
            const SettingValues &values, PathTraceSettings base)
        {
            const auto path_tracing_enabled = Read<bool>(values, "path_tracing_enabled");
            const auto hybrid_shadows = Read<bool>(values, "hybrid_ray_query_shadows_enabled");
            const auto visibility_method = Read<int64_t>(values, "visibility_method");
            const auto samples = Read<uint64_t>(values, "samples_per_dispatch");
            const auto bounces = Read<uint64_t>(values, "maximum_continuation_bounces");
            const auto reconstruction = Read<int64_t>(values, "reconstruction");
            const auto moving_reconstruction =
                Read<int64_t>(values, "adaptive_moving_reconstruction");
            const auto light_sampling = Read<int64_t>(values, "direct_light_sampling");
            const auto sampling_policy = Read<int64_t>(values, "sampling_policy");
            const auto moving_samples = Read<uint64_t>(values, "moving_samples_per_dispatch");
            const auto settled_samples = Read<uint64_t>(values, "settled_samples_per_dispatch");
            const auto quality_2spp_samples =
                Read<uint64_t>(values, "quality_2spp_samples_per_dispatch");
            const auto quality_2spp_threshold =
                Read<uint64_t>(values, "quality_2spp_sample_threshold");
            const auto quality_1spp_threshold =
                Read<uint64_t>(values, "quality_1spp_sample_threshold");
            const auto maintenance_samples =
                Read<uint64_t>(values, "quality_maintenance_samples_per_dispatch");
            const auto settle_frames = Read<uint64_t>(values, "settle_frame_threshold");
            const auto translation_threshold =
                Read<double>(values, "camera_translation_threshold");
            const auto rotation_threshold =
                Read<double>(values, "camera_rotation_threshold_degrees");
            const auto fits_u32 = [](const std::optional<uint64_t> &value)
            {
                return value && *value <= std::numeric_limits<uint32_t>::max();
            };
            if (!path_tracing_enabled || !hybrid_shadows || !samples || !bounces ||
                !visibility_method || !reconstruction || !moving_reconstruction ||
                !light_sampling || !sampling_policy || !fits_u32(samples) ||
                !fits_u32(bounces) || !fits_u32(moving_samples) ||
                !fits_u32(settled_samples) || !fits_u32(quality_2spp_samples) ||
                !fits_u32(quality_2spp_threshold) || !fits_u32(quality_1spp_threshold) ||
                !fits_u32(maintenance_samples) || !fits_u32(settle_frames) ||
                !translation_threshold || !rotation_threshold)
            {
                return std::nullopt;
            }

            base.path_tracing_enabled = *path_tracing_enabled;
            base.hybrid_ray_query_shadows_enabled = *hybrid_shadows;
            base.samples_per_dispatch = static_cast<uint32_t>(*samples);
            base.maximum_continuation_bounces = static_cast<uint32_t>(*bounces);
            switch (*visibility_method)
            {
            case 0: base.visibility_method = PathTraceVisibilityMethod::RayPipeline; break;
            case 1: base.visibility_method = PathTraceVisibilityMethod::RayQuery; break;
            default: return std::nullopt;
            }
            const auto parse_reconstruction = [](int64_t value)
                -> std::optional<PathTraceReconstruction>
            {
                switch (value)
                {
                case 0: return PathTraceReconstruction::Raw;
                case 1: return PathTraceReconstruction::GuidedPreview;
                case 2: return PathTraceReconstruction::VarianceDenoise;
                default: return std::nullopt;
                }
            };
            const auto parsed_reconstruction = parse_reconstruction(*reconstruction);
            const auto parsed_moving_reconstruction =
                parse_reconstruction(*moving_reconstruction);
            if (!parsed_reconstruction || !parsed_moving_reconstruction)
            {
                return std::nullopt;
            }
            base.reconstruction = *parsed_reconstruction;
            base.adaptive_moving_reconstruction = *parsed_moving_reconstruction;
            switch (*light_sampling)
            {
            case 0: base.direct_light_sampling = PathTraceDirectLightSampling::AllLights; break;
            case 1: base.direct_light_sampling = PathTraceDirectLightSampling::UniformOneLight; break;
            default: return std::nullopt;
            }
            switch (*sampling_policy)
            {
            case 0:
                base.sampling_policy = PathTraceSamplingPolicy::Fixed;
                break;
            case 1:
                base.sampling_policy = PathTraceSamplingPolicy::AdaptiveCameraMotion;
                break;
            default:
                return std::nullopt;
            }
            switch (*reconstruction)
            {
            case 0:
                base.reconstruction = PathTraceReconstruction::Raw;
                break;
            case 1:
                base.reconstruction = PathTraceReconstruction::GuidedPreview;
                break;
            case 2:
                base.reconstruction = PathTraceReconstruction::VarianceDenoise;
                break;
            default:
                return std::nullopt;
            }
            base.moving_samples_per_dispatch = static_cast<uint32_t>(*moving_samples);
            base.settled_samples_per_dispatch = static_cast<uint32_t>(*settled_samples);
            base.quality_2spp_samples_per_dispatch =
                static_cast<uint32_t>(*quality_2spp_samples);
            base.quality_2spp_sample_threshold =
                static_cast<uint32_t>(*quality_2spp_threshold);
            base.quality_1spp_sample_threshold =
                static_cast<uint32_t>(*quality_1spp_threshold);
            base.quality_maintenance_samples_per_dispatch =
                static_cast<uint32_t>(*maintenance_samples);
            base.settle_frame_threshold = static_cast<uint32_t>(*settle_frames);
            base.camera_translation_threshold = static_cast<float>(*translation_threshold);
            base.camera_rotation_threshold_degrees = static_cast<float>(*rotation_threshold);
            if (!IsValidPathTraceSettings(base))
            {
                return std::nullopt;
            }
            return base;
        }

        std::optional<ScreenSpaceAoSettings> ConvertAoSettings(const SettingValues &values)
        {
            const auto enabled = Read<bool>(values, "enabled");
            const auto radius = Read<double>(values, "radius");
            const auto bias = Read<double>(values, "bias");
            const auto strength = Read<double>(values, "strength");
            const auto quality = Read<int64_t>(values, "quality");
            if (!enabled || !radius || !bias || !strength || !quality ||
                !std::isfinite(*radius) || !std::isfinite(*bias) ||
                !std::isfinite(*strength) || *radius <= 0.0 || *bias < 0.0 ||
                *bias > *radius * 0.5 || *strength < 0.0 || *strength > 4.0)
            {
                return std::nullopt;
            }
            ScreenSpaceAoQuality parsed_quality{};
            switch (*quality)
            {
            case 0: parsed_quality = ScreenSpaceAoQuality::Low; break;
            case 1: parsed_quality = ScreenSpaceAoQuality::Medium; break;
            case 2: parsed_quality = ScreenSpaceAoQuality::High; break;
            default: return std::nullopt;
            }
            return ScreenSpaceAoSettings{*enabled, static_cast<float>(*radius),
                                          static_cast<float>(*bias),
                                          static_cast<float>(*strength), parsed_quality};
        }

        std::optional<TemporalAaSettings> ConvertTaaSettings(const SettingValues &values)
        {
            const auto enabled = Read<bool>(values, "enabled");
            const auto history_weight = Read<double>(values, "history_weight");
            if (!enabled || !history_weight || !std::isfinite(*history_weight) ||
                *history_weight < 0.0 || *history_weight > 0.95)
            {
                return std::nullopt;
            }
            return TemporalAaSettings{*enabled, static_cast<float>(*history_weight)};
        }

        std::optional<HeightFogSettings> ConvertFogSettings(const SettingValues &values)
        {
            const auto enabled = Read<bool>(values, "enabled");
            const auto affect_sky = Read<bool>(values, "affect_sky");
            const auto density = Read<double>(values, "density");
            const auto reference_height = Read<double>(values, "reference_height");
            const auto height_falloff = Read<double>(values, "height_falloff");
            const auto max_distance = Read<double>(values, "max_distance");
            const auto red = Read<double>(values, "red");
            const auto green = Read<double>(values, "green");
            const auto blue = Read<double>(values, "blue");
            if (!enabled || !affect_sky || !density || !reference_height ||
                !height_falloff || !max_distance || !red || !green || !blue)
            {
                return std::nullopt;
            }
            HeightFogSettings settings{*enabled, *affect_sky, static_cast<float>(*density),
                static_cast<float>(*reference_height), static_cast<float>(*height_falloff),
                static_cast<float>(*max_distance), {static_cast<float>(*red),
                    static_cast<float>(*green), static_cast<float>(*blue)}};
            return IsValidHeightFogSettings(settings)
                ? std::optional<HeightFogSettings>{settings} : std::nullopt;
        }

        std::optional<bool> ReadResolvedBool(
            const SettingsResolvedSnapshot &snapshot,
            const char *section_key, const char *property_key)
        {
            for (const ResolvedSettingsSection &section : snapshot.sections)
            {
                if (section.key != section_key)
                {
                    continue;
                }
                const auto property = section.properties.find(property_key);
                if (property != section.properties.end())
                    return property->second.value.TryGet<bool>() != nullptr
                               ? std::optional<bool>{*property->second.value.TryGet<bool>()}
                               : std::nullopt;
            }
            return std::nullopt;
        }

        std::optional<SettingValues> ReadResolvedSectionValues(
            const SettingsResolvedSnapshot &snapshot, const char *section_key)
        {
            for (const ResolvedSettingsSection &section : snapshot.sections)
            {
                if (section.key != section_key)
                {
                    continue;
                }
                SettingValues values;
                for (const auto &[key, property] : section.properties)
                {
                    values.emplace(key, property.value);
                }
                return values;
            }
            return std::nullopt;
        }

        SettingSectionDefinition MakeStartupSection()
        {
            SettingSectionDefinition section{};
            section.key = "render.startup";
            section.label = "Rendering startup";
            section.category = "Rendering";
            section.properties.push_back(
                BoolProperty("ray_tracing_support", "Ray tracing support", true));
            section.allowed_save_scopes = {SettingSaveScope::Project, SettingSaveScope::User};
            section.apply_policy = SettingApplyPolicy::RestartRequired;
            return section;
        }

        SettingSectionDefinition MakePathTraceSection()
        {
            SettingSectionDefinition section{};
            section.key = "render.path_tracing";
            section.label = "Path tracing";
            section.category = "Rendering";
            section.properties = {
                BoolProperty("path_tracing_enabled", "Path tracing", true),
                BoolProperty("hybrid_ray_query_shadows_enabled", "Hybrid ray-query shadows", true),
                EnumProperty("visibility_method", "Visibility method", 0,
                    {{0, "Ray pipeline"}, {1, "Ray query"}}),
                UnsignedProperty("samples_per_dispatch", "Samples per dispatch", 4, 1, 16, 1),
                UnsignedProperty("maximum_continuation_bounces", "Maximum continuation bounces", 8, 0, 32, 1),
                EnumProperty("reconstruction", "Reconstruction", 0,
                    {{0, "Raw"}, {1, "Guided preview"}, {2, "Variance denoise"}}),
                EnumProperty("adaptive_moving_reconstruction", "Moving reconstruction", 1,
                    {{0, "Raw"}, {1, "Guided preview"}, {2, "Variance denoise"}}),
                EnumProperty("direct_light_sampling", "Direct light sampling", 0,
                    {{0, "All lights"}, {1, "Uniform one light"}}),
                EnumProperty("sampling_policy", "Sampling policy", 1,
                    {{0, "Fixed"}, {1, "Adaptive camera motion"}}),
                UnsignedProperty("moving_samples_per_dispatch", "Moving samples per dispatch", 1, 1, 16, 1),
                UnsignedProperty("settled_samples_per_dispatch", "Settled samples per dispatch", 4, 1, 16, 1),
                UnsignedProperty("quality_2spp_samples_per_dispatch", "Quality 2 SPP samples", 2, 1, 16, 1),
                UnsignedProperty("quality_2spp_sample_threshold", "Quality 2 SPP threshold", 100, 1, 999999, 1),
                UnsignedProperty("quality_1spp_sample_threshold", "Quality 1 SPP threshold", 200, 2, 1000000, 1),
                UnsignedProperty("quality_maintenance_samples_per_dispatch", "Maintenance samples", 1, 1, 16, 1),
                UnsignedProperty("settle_frame_threshold", "Settle frame threshold", 8, 1, 120, 1),
                FloatProperty("camera_translation_threshold", "Camera translation threshold", 0.02, 0.0001, 10, 0.001),
                FloatProperty("camera_rotation_threshold_degrees", "Camera rotation threshold (degrees)", 0.2, 0.0001, 180, 0.01),
            };
            section.allowed_save_scopes = {SettingSaveScope::Project, SettingSaveScope::User};
            section.apply_policy = SettingApplyPolicy::Live;
            section.validate = [](const SettingValues &values)
            {
                return ConvertPathTraceSettings(values, {}).has_value()
                           ? SettingValidationResult{}
                           : SettingValidationResult::Reject(
                                 "path-tracing values are outside the supported ranges");
            };
            return section;
        }

        SettingSectionDefinition MakeAoSection()
        {
            SettingSectionDefinition section{};
            section.key = "render.screen_space_ao";
            section.label = "Screen-space ambient occlusion";
            section.category = "Rendering";
            section.properties = {
                BoolProperty("enabled", "Enabled", true),
                FloatProperty("radius", "Radius", 0.65, 0.05, 2.0, 0.01),
                FloatProperty("bias", "Bias", 0.025, 0.0, 1.0, 0.001),
                FloatProperty("strength", "Strength", 1.0, 0.0, 4.0, 0.01),
                EnumProperty("quality", "Quality", 1,
                    {{0, "Low"}, {1, "Medium"}, {2, "High"}}),
            };
            section.allowed_save_scopes = {SettingSaveScope::Project, SettingSaveScope::User};
            section.apply_policy = SettingApplyPolicy::Live;
            section.validate = [](const SettingValues &values)
            {
                return ConvertAoSettings(values).has_value()
                    ? SettingValidationResult{}
                    : SettingValidationResult::Reject("SSAO values are outside the supported ranges");
            };
            return section;
        }

        SettingSectionDefinition MakeTaaSection()
        {
            SettingSectionDefinition section{};
            section.key = "render.temporal_aa";
            section.label = "Temporal anti-aliasing";
            section.category = "Rendering";
            section.properties = {
                BoolProperty("enabled", "Enabled", true),
                FloatProperty("history_weight", "History weight", 0.9, 0.0, 0.95, 0.01),
            };
            section.allowed_save_scopes = {SettingSaveScope::Project, SettingSaveScope::User};
            section.apply_policy = SettingApplyPolicy::Live;
            section.validate = [](const SettingValues &values)
            {
                return ConvertTaaSettings(values).has_value()
                    ? SettingValidationResult{}
                    : SettingValidationResult::Reject("TAA history weight must be in [0, 0.95]");
            };
            return section;
        }

        SettingSectionDefinition MakeFogSection()
        {
            SettingSectionDefinition section{};
            section.key = "render.height_fog";
            section.label = "Height fog";
            section.category = "Rendering";
            section.properties = {
                BoolProperty("enabled", "Enabled", false),
                BoolProperty("affect_sky", "Affect sky", false),
                FloatProperty("density", "Density", 0.02, 0.0, 100.0, 0.001),
                FloatProperty("reference_height", "Reference height", 0.0, -1000000.0, 1000000.0, 0.1),
                FloatProperty("height_falloff", "Height falloff", 0.1, 0.0, 100.0, 0.001),
                FloatProperty("max_distance", "Maximum distance", 100.0, 0.001, 1000000.0, 0.1),
                FloatProperty("red", "Color red", 0.45, 0.0, 65504.0, 0.01),
                FloatProperty("green", "Color green", 0.55, 0.0, 65504.0, 0.01),
                FloatProperty("blue", "Color blue", 0.65, 0.0, 65504.0, 0.01),
            };
            section.allowed_save_scopes = {SettingSaveScope::Project, SettingSaveScope::User};
            section.apply_policy = SettingApplyPolicy::Live;
            section.validate = [](const SettingValues &values)
            {
                return ConvertFogSettings(values).has_value()
                    ? SettingValidationResult{}
                    : SettingValidationResult::Reject("height-fog values are outside the supported ranges");
            };
            return section;
        }

        void CompleteProviderEdit(
            settings::SettingsCompletionHandle completion,
            RenderSettingsApplyOutcome outcome,
            settings::SettingValues active_values)
        {
            switch (outcome)
            {
            case RenderSettingsApplyOutcome::Applied:
                completion.Complete({settings::SettingsProviderOutcome::Applied,
                                     std::move(active_values), {}});
                break;
            case RenderSettingsApplyOutcome::Superseded:
                completion.Complete({settings::SettingsProviderOutcome::Superseded,
                                     std::move(active_values),
                                     "a newer Render request replaced this settings edit"});
                break;
            case RenderSettingsApplyOutcome::Cancelled:
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     std::move(active_values),
                                     "Render shut down before applying this settings edit"});
                break;
            }
        }
    }

    settings::SettingsResult RegisterRenderSettings(
        settings::SettingsRegistry &registry)
    {
        if (settings::SettingsResult result = registry.RegisterSection(MakeStartupSection());
            !result)
        {
            return result;
        }
        if (settings::SettingsResult result = registry.RegisterSection(MakePathTraceSection());
            !result)
        {
            return result;
        }
        if (settings::SettingsResult result = registry.RegisterSection(MakeAoSection()); !result)
        {
            return result;
        }
        if (settings::SettingsResult result = registry.RegisterSection(MakeTaaSection()); !result)
        {
            return result;
        }
        return registry.RegisterSection(MakeFogSection());
    }

    settings::SettingsLayerOverrides MakeRenderSettingsSessionOverrides(
        const RenderSettingsSessionOptions &options)
    {
        settings::SettingsLayerOverrides overrides;
        if (options.ray_tracing_support)
        {
            overrides["render.startup"]["ray_tracing_support"] =
                *options.ray_tracing_support;
        }
        if (options.path_tracing_enabled)
        {
            overrides["render.path_tracing"]["path_tracing_enabled"] =
                *options.path_tracing_enabled;
        }
        return overrides;
    }

    bool ResolveRenderStartupSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        bool &ray_tracing_support) noexcept
    {
        const std::optional<bool> value =
            ReadResolvedBool(snapshot, "render.startup", "ray_tracing_support");
        if (!value)
        {
            return false;
        }
        ray_tracing_support = *value;
        return true;
    }

    PathTraceSettings ResolvePathTraceSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        PathTraceSettings base)
    {
        const auto values = ReadResolvedSectionValues(snapshot, "render.path_tracing");
        const auto converted = values ? ConvertPathTraceSettings(*values, base) : std::nullopt;
        return converted.value_or(base);
    }

    ScreenSpaceAoSettings ResolveScreenSpaceAoSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        ScreenSpaceAoSettings base)
    {
        const auto values = ReadResolvedSectionValues(snapshot, "render.screen_space_ao");
        const auto converted = values ? ConvertAoSettings(*values) : std::nullopt;
        return converted.value_or(base);
    }

    TemporalAaSettings ResolveTemporalAaSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        TemporalAaSettings base)
    {
        const auto values = ReadResolvedSectionValues(snapshot, "render.temporal_aa");
        const auto converted = values ? ConvertTaaSettings(*values) : std::nullopt;
        return converted.value_or(base);
    }

    HeightFogSettings ResolveHeightFogSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        HeightFogSettings base)
    {
        const auto values = ReadResolvedSectionValues(snapshot, "render.height_fog");
        const auto converted = values ? ConvertFogSettings(*values) : std::nullopt;
        return converted.value_or(base);
    }

    settings::SettingValues MakePathTraceSettingValues(
        const PathTraceSettings &settings)
    {
        return {
            {"path_tracing_enabled", settings.path_tracing_enabled},
            {"hybrid_ray_query_shadows_enabled", settings.hybrid_ray_query_shadows_enabled},
            {"visibility_method", static_cast<int64_t>(settings.visibility_method)},
            {"samples_per_dispatch", static_cast<uint64_t>(settings.samples_per_dispatch)},
            {"maximum_continuation_bounces",
             static_cast<uint64_t>(settings.maximum_continuation_bounces)},
            {"adaptive_moving_reconstruction",
             static_cast<int64_t>(settings.adaptive_moving_reconstruction)},
            {"sampling_policy", static_cast<int64_t>(settings.sampling_policy)},
            {"reconstruction", static_cast<int64_t>(settings.reconstruction)},
            {"direct_light_sampling", static_cast<int64_t>(settings.direct_light_sampling)},
            {"moving_samples_per_dispatch", static_cast<uint64_t>(settings.moving_samples_per_dispatch)},
            {"settled_samples_per_dispatch", static_cast<uint64_t>(settings.settled_samples_per_dispatch)},
            {"quality_2spp_samples_per_dispatch", static_cast<uint64_t>(settings.quality_2spp_samples_per_dispatch)},
            {"quality_2spp_sample_threshold", static_cast<uint64_t>(settings.quality_2spp_sample_threshold)},
            {"quality_1spp_sample_threshold", static_cast<uint64_t>(settings.quality_1spp_sample_threshold)},
            {"quality_maintenance_samples_per_dispatch", static_cast<uint64_t>(settings.quality_maintenance_samples_per_dispatch)},
            {"settle_frame_threshold", static_cast<uint64_t>(settings.settle_frame_threshold)},
            {"camera_translation_threshold", static_cast<double>(settings.camera_translation_threshold)},
            {"camera_rotation_threshold_degrees", static_cast<double>(settings.camera_rotation_threshold_degrees)},
        };
    }

    settings::SettingValues MakeScreenSpaceAoSettingValues(
        const ScreenSpaceAoSettings &settings)
    {
        return {
            {"enabled", settings.enabled},
            {"radius", static_cast<double>(settings.radius)},
            {"bias", static_cast<double>(settings.bias)},
            {"strength", static_cast<double>(settings.strength)},
            {"quality", static_cast<int64_t>(settings.quality)},
        };
    }

    settings::SettingValues MakeTemporalAaSettingValues(
        const TemporalAaSettings &settings)
    {
        return {
            {"enabled", settings.enabled},
            {"history_weight", static_cast<double>(settings.history_weight)},
        };
    }

    settings::SettingValues MakeHeightFogSettingValues(
        const HeightFogSettings &settings)
    {
        return {
            {"enabled", settings.enabled},
            {"affect_sky", settings.affect_sky},
            {"density", static_cast<double>(settings.density)},
            {"reference_height", static_cast<double>(settings.reference_height)},
            {"height_falloff", static_cast<double>(settings.height_falloff)},
            {"max_distance", static_cast<double>(settings.max_distance)},
            {"red", static_cast<double>(settings.color[0])},
            {"green", static_cast<double>(settings.color[1])},
            {"blue", static_cast<double>(settings.color[2])},
        };
    }

    void RenderSettingsProvider::Apply(
        const settings::SettingsApplyRequest &request,
        settings::SettingsCompletionHandle completion)
    {
        if (request.section_key == "render.path_tracing")
        {
            const PathTraceSettings current = render_system_.GetRequestedPathTraceSettings();
            const auto candidate = ConvertPathTraceSettings(request.candidate_values, current);
            if (!candidate)
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakePathTraceSettingValues(current),
                                     "path-tracing values could not be converted to the typed contract"});
                return;
            }
            if (!render_system_.HasSceneRenderer())
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakePathTraceSettingValues(current),
                                     "scene renderer is unavailable; configured settings were not applied by this host"});
                return;
            }
            if (candidate->path_tracing_enabled && !current.path_tracing_enabled &&
                !render_system_.IsPathTracingAvailable())
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakePathTraceSettingValues(current),
                                     "path-tracing resources are unavailable; enable RT support and restart"});
                return;
            }

            const bool queued = render_system_.RequestPathTraceSettings(
                *candidate,
                [completion](PathTraceSettingsApplyResult result) mutable
                {
                    CompleteProviderEdit(completion, result.outcome,
                                         MakePathTraceSettingValues(result.active_settings));
                });
            if (!queued)
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakePathTraceSettingValues(current),
                                     "Render rejected invalid path-tracing settings"});
            }
            return;
        }

        if (request.section_key == "render.screen_space_ao")
        {
            const ScreenSpaceAoSettings current =
                render_system_.GetRequestedScreenSpaceAoSettings();
            const auto candidate = ConvertAoSettings(request.candidate_values);
            if (!candidate)
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeScreenSpaceAoSettingValues(current),
                                     "SSAO values could not be converted to the typed contract"});
                return;
            }
            if (!render_system_.HasSceneRenderer())
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeScreenSpaceAoSettingValues(current),
                                     "scene renderer is unavailable; configured settings were not applied by this host"});
                return;
            }
            if (!render_system_.RequestScreenSpaceAoSettings(
                    *candidate, [completion](ScreenSpaceAoSettingsApplyResult result) mutable
                    {
                        CompleteProviderEdit(completion, result.outcome,
                            MakeScreenSpaceAoSettingValues(result.active_settings));
                    }))
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeScreenSpaceAoSettingValues(current),
                                     "Render rejected invalid SSAO settings"});
            }
            return;
        }

        if (request.section_key == "render.temporal_aa")
        {
            const TemporalAaSettings current =
                render_system_.GetRequestedTemporalAaSettings();
            const auto candidate = ConvertTaaSettings(request.candidate_values);
            if (!candidate)
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeTemporalAaSettingValues(current),
                                     "TAA values could not be converted to the typed contract"});
                return;
            }
            if (!render_system_.HasSceneRenderer())
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeTemporalAaSettingValues(current),
                                     "scene renderer is unavailable; configured settings were not applied by this host"});
                return;
            }
            if (!render_system_.RequestTemporalAaSettings(
                    *candidate, [completion](TemporalAaSettingsApplyResult result) mutable
                    {
                        CompleteProviderEdit(completion, result.outcome,
                            MakeTemporalAaSettingValues(result.active_settings));
                    }))
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeTemporalAaSettingValues(current),
                                     "Render rejected invalid TAA settings"});
            }
            return;
        }

        if (request.section_key == "render.height_fog")
        {
            const HeightFogSettings current = render_system_.GetRequestedHeightFogSettings();
            const auto candidate = ConvertFogSettings(request.candidate_values);
            if (!candidate)
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeHeightFogSettingValues(current),
                                     "height-fog values could not be converted to the typed contract"});
                return;
            }
            if (!render_system_.HasSceneRenderer())
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeHeightFogSettingValues(current),
                                     "scene renderer is unavailable; configured settings were not applied by this host"});
                return;
            }
            if (!render_system_.RequestHeightFogSettings(
                    *candidate, [completion](HeightFogSettingsApplyResult result) mutable
                    {
                        CompleteProviderEdit(completion, result.outcome,
                            MakeHeightFogSettingValues(result.active_settings));
                    }))
            {
                completion.Complete({settings::SettingsProviderOutcome::Rejected,
                                     MakeHeightFogSettingValues(current),
                                     "Render rejected invalid height-fog settings"});
            }
            return;
        }

        completion.Complete({settings::SettingsProviderOutcome::Rejected, {},
                             "Render settings provider does not own section '" +
                                 request.section_key + "'"});
    }
}
