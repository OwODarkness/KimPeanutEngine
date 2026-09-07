#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_VISIBILITY_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_VISIBILITY_H

#include <vector>
#include <limits>

#include "math/math_header.h"
#include "render/render_world/mesh_proxy.h"

namespace kpengine::render
{
    class RenderResourceResolver;

    struct VisibleMeshSection
    {
        MeshProxy proxy;
        spatial::AABB world_bounds{};
        uint32_t section_index = std::numeric_limits<uint32_t>::max();
    };

    // Per-view render policy. RenderWorld owns proxy storage; this value-only
    // algorithm derives the visible draw candidates for one pass and camera.
    class SceneVisibility
    {
    public:
        static std::vector<MeshProxy> BuildVisibleProxies(
            const Matrix4f &view_projection, const std::vector<MeshProxy> &proxies);

        // Performs the cheap proxy broad phase first, then rejects individual
        // native mesh sections using their persisted local bounds. A missing
        // or invalid section bound remains visible conservatively.
        static std::vector<VisibleMeshSection> BuildVisibleSections(
            const Matrix4f &view_projection, const std::vector<MeshProxy> &proxies,
            const RenderResourceResolver &resource_resolver);

        // Shadow fitting and non-camera light volumes need section identities
        // without applying a camera frustum.
        static std::vector<VisibleMeshSection> BuildSectionCandidates(
            const std::vector<MeshProxy> &proxies,
            const RenderResourceResolver &resource_resolver);
    };
}

#endif
