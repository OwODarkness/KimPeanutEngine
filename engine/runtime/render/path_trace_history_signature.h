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
        uint32_t pipeline_id = 0;
        uint32_t pipeline_generation = 0;
        uint64_t shader_signature = 0;
        uint32_t output_pipeline_id = 0;
        uint32_t output_pipeline_generation = 0;
        uint64_t output_shader_signature = 0;
        uint32_t probe_mode = 0;
        float exposure = 1.0f;
        uint32_t tone_map_operator = 0;
        uint32_t output_transfer = 0;
        std::array<float, 12> light_parameters{};
        std::array<float, 4> ray_parameters{};
        std::array<uint32_t, 4> integrator_parameters{};
        uint32_t rng_seed = 0;
        uint32_t rng_policy_version = 0;
        std::array<float, 16> view_projection{};
        std::array<float, 3> camera_position{};
    };

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
        add(input.pipeline_id);
        add(input.pipeline_generation);
        add(input.shader_signature);
        add(input.output_pipeline_id);
        add(input.output_pipeline_generation);
        add(input.output_shader_signature);
        add(input.probe_mode);
        add_float(input.exposure);
        add(input.tone_map_operator);
        add(input.output_transfer);
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
