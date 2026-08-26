#include "render/render_world/scene_visibility.h"

#include "render/render_world/frustum.h"

namespace kpengine::render
{
    std::vector<MeshProxy> SceneVisibility::BuildVisibleProxies(
        const Matrix4f &view_projection, const std::vector<MeshProxy> &proxies)
    {
        const Frustum frustum = Frustum::FromViewProjection(view_projection);
        std::vector<MeshProxy> visible_proxies;
        visible_proxies.reserve(proxies.size());
        for (const MeshProxy &proxy : proxies)
        {
            if (proxy.flags.visible && frustum.Intersects(proxy.world_bounds))
            {
                visible_proxies.push_back(proxy);
            }
        }
        return visible_proxies;
    }
}
