#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_MESH_PROXY_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_MESH_PROXY_H

#include "base/handle.h"
#include "graphics/backend/common/api.h"
#include "math/math_header.h"
#include "render/material/material_system.h"
#include "render/render_source.h"
#include "spatial/aabb.h"

namespace kpengine::render
{
    struct RenderableTag {};
    using RenderableHandle = Handle<RenderableTag>;

    // Render-owned snapshot of one static mesh renderable. It intentionally has
    // no draw operation, component pointer, or native API object. RenderWorld
    // will own instances and assign the generational handle in a later phase.
    struct MeshProxy
    {
        RenderableHandle handle;
        graphics::MeshHandle mesh;
        MaterialInstanceHandle material;
        Transform3f world_transform;
        spatial::AABB world_bounds{};
        RenderableFlags flags;
        int lod_bias = 0;
    };
}

#endif
