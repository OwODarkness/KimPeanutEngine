#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_MESH_PROXY_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_MESH_PROXY_H

#include <array>

#include "base/handle.h"
#include "graphics/backend/common/api.h"
#include "math/math_header.h"
#include "render/material/material_system.h"

namespace kpengine::render
{
    struct RenderableTag {};
    using RenderableHandle = Handle<RenderableTag>;

    // AABB storage remains temporary until World owns the canonical spatial type.
    // It is {min_x, min_y, min_z, max_x, max_y, max_z}; do not expose it outside render.
    using AABB = std::array<float, 6>;

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
        MaterialInstanceHandle material;
        Transform3f world_transform;
        AABB world_bounds{};
        RenderableFlags flags;
        int lod_bias = 0;
    };
}

#endif
