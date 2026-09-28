#ifndef KPENGINE_RUNTIME_RENDER_RAY_TRACING_SCENE_SECTIONS_H
#define KPENGINE_RUNTIME_RENDER_RAY_TRACING_SCENE_SECTIONS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "graphics/backend/common/api.h"
#include "render/material/material_system.h"

namespace kpengine::render::detail
{
    struct RayTracingSectionKey
    {
        graphics::MeshHandle mesh;
        std::vector<uint32_t> section_indices;

        bool operator==(const RayTracingSectionKey &) const = default;
    };

    struct RayTracingSectionKeyHash
    {
        std::size_t operator()(const RayTracingSectionKey &key) const noexcept
        {
            std::size_t hash = std::hash<graphics::MeshHandle>{}(key.mesh);
            for (uint32_t section : key.section_indices)
                hash ^= std::hash<uint32_t>{}(section) + 0x9e3779b9U +
                        (hash << 6U) + (hash >> 2U);
            return hash;
        }
    };

    inline RayTracingSectionKey MakeOpaqueRayTracingSectionKey(
        graphics::MeshHandle mesh,
        std::span<const std::optional<MaterialDrawClass>> draw_classes)
    {
        RayTracingSectionKey key{mesh, {}};
        for (std::size_t section = 0; section < draw_classes.size(); ++section)
        {
            // Match the current raster surface domain; blended transport is unsupported.
            if (draw_classes[section] == MaterialDrawClass::Opaque)
                key.section_indices.push_back(static_cast<uint32_t>(section));
        }
        return key;
    }
}

#endif
