#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_DRAW_LIST_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_SCENE_DRAW_LIST_H

#include <vector>

#include "graphics/backend/common/api.h"
#include "render/material/material_system.h"
#include "render/render_world/mesh_proxy.h"

namespace kpengine::render
{
    class RenderResourceResolver;

    // A draw-ready proxy plus the resolved pipeline used as its opaque batch key.
    struct SceneDrawItem
    {
        MeshProxy proxy;
        graphics::PipelineHandle pipeline;
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
                                    const RenderResourceResolver &resource_resolver);
        static void SortOpaque(std::vector<SceneDrawItem> &items);
    };
}

#endif
