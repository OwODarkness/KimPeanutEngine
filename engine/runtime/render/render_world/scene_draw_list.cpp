#include "render/render_world/scene_draw_list.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace kpengine::render
{
    namespace
    {
        bool IsLessOpaqueBatchKey(const SceneDrawItem &lhs, const SceneDrawItem &rhs)
        {
            return std::tie(lhs.pipeline.id, lhs.pipeline.generation,
                            lhs.proxy.material.id, lhs.proxy.material.generation,
                            lhs.proxy.mesh.id, lhs.proxy.mesh.generation,
                            lhs.section_index) <
                   std::tie(rhs.pipeline.id, rhs.pipeline.generation,
                            rhs.proxy.material.id, rhs.proxy.material.generation,
                            rhs.proxy.mesh.id, rhs.proxy.mesh.generation,
                            rhs.section_index);
        }
    }

    void SceneDrawListBuilder::SortOpaque(std::vector<SceneDrawItem> &items)
    {
        std::sort(items.begin(), items.end(), IsLessOpaqueBatchKey);
    }

    void SceneDrawListBuilder::SortOpaqueFrontToBack(
        std::vector<SceneDrawItem> &items, const Vector3f &camera_position,
        const Vector3f &camera_forward)
    {
        const Vector3f forward = camera_forward.GetSafetyNormalize();
        const auto depth = [&camera_position, &forward](const SceneDrawItem &item)
        {
            const spatial::AABB &bounds = item.proxy.world_bounds;
            const Vector3f center = bounds.IsValid()
                                         ? (bounds.min_ + bounds.max_) * 0.5f
                                         : item.proxy.world_transform.position_;
            const float result = (center - camera_position).DotProduct(forward);
            return std::isfinite(result) ? result : std::numeric_limits<float>::max();
        };

        std::stable_sort(items.begin(), items.end(), [&depth](const SceneDrawItem &lhs,
                                                               const SceneDrawItem &rhs)
        {
            const float lhs_depth = depth(lhs);
            const float rhs_depth = depth(rhs);
            if (lhs_depth != rhs_depth)
            {
                return lhs_depth < rhs_depth;
            }
            return IsLessOpaqueBatchKey(lhs, rhs);
        });
    }
}
