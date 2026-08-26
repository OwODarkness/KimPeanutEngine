#include "render/render_world/scene_draw_list.h"

#include <algorithm>
#include <tuple>

namespace kpengine::render
{
    namespace
    {
        bool IsLessOpaqueBatchKey(const SceneDrawItem &lhs, const SceneDrawItem &rhs)
        {
            return std::tie(lhs.pipeline.id, lhs.pipeline.generation,
                            lhs.proxy.material.id, lhs.proxy.material.generation,
                            lhs.proxy.mesh.id, lhs.proxy.mesh.generation) <
                   std::tie(rhs.pipeline.id, rhs.pipeline.generation,
                            rhs.proxy.material.id, rhs.proxy.material.generation,
                            rhs.proxy.mesh.id, rhs.proxy.mesh.generation);
        }
    }

    void SceneDrawListBuilder::SortOpaque(std::vector<SceneDrawItem> &items)
    {
        std::sort(items.begin(), items.end(), IsLessOpaqueBatchKey);
    }
}
