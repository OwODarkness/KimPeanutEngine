#ifndef KPENGINE_RUNTIME_RENDER_RAY_TRACING_SCENE_SIGNATURE_H
#define KPENGINE_RUNTIME_RENDER_RAY_TRACING_SCENE_SIGNATURE_H

#include <cstdint>
#include <span>

#include "graphics/backend/common/ray_tracing.h"

namespace kpengine::render::detail
{
    inline uint64_t RayTracingGeometrySignature(
        std::span<const graphics::RayTracingGeometryDesc> geometries) noexcept
    {
        uint64_t signature = 1469598103934665603ull;
        const auto add = [&signature](uint64_t value) {
            signature ^= value;
            signature *= 1099511628211ull;
        };
        add(geometries.size());
        for (const graphics::RayTracingGeometryDesc &geometry : geometries)
        {
            add(geometry.vertex_buffer.id);
            add(geometry.vertex_buffer.generation);
            add(geometry.vertex_offset);
            add(geometry.vertex_stride);
            add(geometry.vertex_count);
            add(geometry.index_buffer.id);
            add(geometry.index_buffer.generation);
            add(geometry.index_offset);
            add(geometry.index_count);
            add(static_cast<uint64_t>(geometry.index_type));
        }
        return signature;
    }
}

#endif
