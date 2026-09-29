#ifndef KPENGINE_RUNTIME_RENDER_PATH_TRACE_HISTORY_SIGNATURE_H
#define KPENGINE_RUNTIME_RENDER_PATH_TRACE_HISTORY_SIGNATURE_H

#include <array>
#include <bit>
#include <cstdint>

namespace kpengine::render::detail
{
    struct PathTraceHistorySignatureInput
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t scene_signature = 0;
        uint64_t geometry_count = 0;
        uint64_t material_signature = 0;
        uint64_t lighting_signature = 0;
        uint32_t pipeline_id = 0;
        uint32_t pipeline_generation = 0;
        uint64_t shader_signature = 0;
        uint32_t probe_mode = 0;
        std::array<float, 12> light_parameters{};
        std::array<float, 4> ray_parameters{};
        uint32_t batch_samples_per_dispatch = 1;
        std::array<uint32_t, 3> integrator_parameters{};
        uint32_t rng_seed = 0;
        uint32_t rng_policy_version = 0;
        std::array<float, 16> view_projection{};
        std::array<float, 3> camera_position{};
    };

    inline const char *DescribePathTraceHistoryChange(
        const PathTraceHistorySignatureInput &previous,
        const PathTraceHistorySignatureInput &current) noexcept
    {
        if (previous.width != current.width || previous.height != current.height)
            return "viewport_changed";
        if (previous.view_projection != current.view_projection ||
            previous.camera_position != current.camera_position)
            return "camera_changed";
        if (previous.scene_signature != current.scene_signature ||
            previous.geometry_count != current.geometry_count)
            return "scene_changed";
        if (previous.material_signature != current.material_signature)
            return "materials_changed";
        if (previous.lighting_signature != current.lighting_signature ||
            previous.light_parameters != current.light_parameters)
            return "lighting_changed";
        if (previous.pipeline_id != current.pipeline_id ||
            previous.pipeline_generation != current.pipeline_generation ||
            previous.shader_signature != current.shader_signature)
            return "pipeline_changed";
        return "integrator_changed";
    }

    inline uint64_t ComputePathTraceHistorySignature(
        const PathTraceHistorySignatureInput &input) noexcept
    {
        uint64_t signature = 1469598103934665603ull;
        const auto add = [&signature](uint64_t value) {
            signature ^= value;
            signature *= 1099511628211ull;
        };
        const auto add_float = [&add](float value) {
            add(std::bit_cast<uint32_t>(value));
        };

        add(input.width);
        add(input.height);
        add(input.scene_signature);
        add(input.geometry_count);
        add(input.material_signature);
        add(input.lighting_signature);
        add(input.pipeline_id);
        add(input.pipeline_generation);
        add(input.shader_signature);
        add(input.probe_mode);
        for (const float value : input.light_parameters)
        {
            add_float(value);
        }
        for (const float value : input.ray_parameters)
        {
            add_float(value);
        }
        for (const uint32_t value : input.integrator_parameters)
        {
            add(value);
        }
        add(input.rng_seed);
        add(input.rng_policy_version);
        for (const float value : input.view_projection)
        {
            add_float(value);
        }
        for (const float value : input.camera_position)
        {
            add_float(value);
        }
        return signature;
    }
}

#endif
