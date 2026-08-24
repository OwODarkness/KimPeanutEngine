#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_MESH_PROXY_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_MESH_PROXY_H

#include <array>
#include <cstdint>
#include <limits>

#include "base/handle.h"
#include "graphics/backend/common/api.h"
#include "math/math_header.h"

namespace kpengine::render
{
    struct RenderableTag {};
    using RenderableHandle = Handle<RenderableTag>;

    // Temporary aliases until the World bounds type and render material-instance
    // system are reconstructed. AABB storage is {min_x, min_y, min_z,
    // max_x, max_y, max_z}; do not expose this representation outside render.
    using AABB = std::array<float, 6>;
    using MaterialInstanceHandle = uint64_t;
    constexpr MaterialInstanceHandle kInvalidMaterialInstanceHandle =
        std::numeric_limits<MaterialInstanceHandle>::max();

    struct RenderableFlags
    {
        bool visible = true;
        bool opaque = true;
        bool casts_shadow = true;
    };

    // Render-owned snapshot of one static mesh renderable. It intentionally has
    // no draw operation, component pointer, or native API object. RenderWorld
    // will own instances and assign the generational handle in a later phase.
    struct MeshProxy
    {
        RenderableHandle handle;
        graphics::MeshHandle mesh;
        MaterialInstanceHandle material = kInvalidMaterialInstanceHandle;
        Transform3f world_transform;
        AABB world_bounds{};
        RenderableFlags flags;
        int lod_bias = 0;
    };
}

#endif
