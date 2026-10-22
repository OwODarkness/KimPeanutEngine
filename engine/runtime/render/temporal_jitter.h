#ifndef KPENGINE_RUNTIME_RENDER_TEMPORAL_JITTER_H
#define KPENGINE_RUNTIME_RENDER_TEMPORAL_JITTER_H

#include <cstdint>
#include <cstddef>

#include "math/math_header.h"

namespace kpengine::render
{
    inline float TemporalHalton(uint32_t index, uint32_t base) noexcept
    {
        float result = 0.0f;
        float fraction = 1.0f / static_cast<float>(base);
        while (index != 0)
        {
            result += fraction * static_cast<float>(index % base);
            index /= base;
            fraction /= static_cast<float>(base);
        }
        return result;
    }

    inline Vector2f TemporalJitterUv(uint32_t submitted_frame,
                                     uint32_t width, uint32_t height) noexcept
    {
        if (width == 0 || height == 0)
            return {};
        const uint32_t sample = submitted_frame % 8u + 1u;
        return {(TemporalHalton(sample, 2u) - 0.5f) / static_cast<float>(width),
                (TemporalHalton(sample, 3u) - 0.5f) / static_cast<float>(height)};
    }

    inline Matrix4f ApplyTemporalJitter(Matrix4f projection,
                                        Vector2f jitter_uv) noexcept
    {
        const float clip_x = 2.0f * jitter_uv.x_;
        const float clip_y = -2.0f * jitter_uv.y_;
        for (size_t column = 0; column < 4; ++column)
        {
            projection[0][column] += clip_x * projection[3][column];
            projection[1][column] += clip_y * projection[3][column];
        }
        return projection;
    }
}

#endif
