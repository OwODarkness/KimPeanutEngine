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
        uint32_t pipeline_id = 0;
        uint32_t pipeline_generation = 0;
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
        add(input.pipeline_id);
        add(input.pipeline_generation);
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
