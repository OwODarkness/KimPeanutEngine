#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_DRAW_LIST_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_DRAW_LIST_H

#include <cstdint>
#include <limits>
#include <vector>

#include "graphics/backend/common/api.h"
#include "render/material/material_system.h"
#include "render/render_world/mesh_proxy.h"
#include "render/render_world/scene_visibility.h"

namespace kpengine::render
{
    class RenderResourceResolver;

    // A compact draw packet plus the resolved pipeline used as its opaque batch
    // key. The packet proxy intentionally has no section_materials ownership.
    struct SceneDrawItem
    {
        MeshProxy proxy;
        graphics::PipelineHandle pipeline;
        // Index into the resolver-owned mesh section list. The sentinel means
        // that the proxy has no section metadata and uses the legacy full draw.
        uint32_t section_index = std::numeric_limits<uint32_t>::max();
    };

    struct SceneDrawLists
    {
        std::vector<SceneDrawItem> opaque;
        // Alpha-blend items retain snapshot order until a pass supplies a
        // camera-depth sorting policy.
        std::vector<SceneDrawItem> alpha_blend;
    };

    // Converts visible proxies into pass draw lists. It owns no proxy, material,
    // or GPU resource; it only reads their render-owned resolved state.
    class SceneDrawListBuilder
    {
    public:
        static SceneDrawLists Build(const std::vector<MeshProxy> &visible_proxies,
                                    const MaterialSystem &materials,
                                    const RenderResourceResolver &resource_resolver,
                                    MaterialPass pass);
        static SceneDrawLists Build(const std::vector<VisibleMeshSection> &visible_sections,
                                    const MaterialSystem &materials,
                                    const RenderResourceResolver &resource_resolver,
                                    MaterialPass pass);
        static void SortOpaque(std::vector<SceneDrawItem> &items);
        // Orders opaque work by increasing camera distance so completed depth
        // can reject later fragments before their material shader runs.
        static void SortOpaqueFrontToBack(std::vector<SceneDrawItem> &items,
                                          const Vector3f &camera_position,
                                          const Vector3f &camera_forward);
    };
}

#endif
