#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_VISIBILITY_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_VISIBILITY_H

#include <vector>

#include "math/math_header.h"
#include "render/render_world/mesh_proxy.h"

namespace kpengine::render
{
    // Per-view render policy. RenderWorld owns proxy storage; this value-only
    // algorithm derives the visible draw candidates for one pass and camera.
    class SceneVisibility
    {
    public:
        static std::vector<MeshProxy> BuildVisibleProxies(
            const Matrix4f &view_projection, const std::vector<MeshProxy> &proxies);
    };
}

#endif
