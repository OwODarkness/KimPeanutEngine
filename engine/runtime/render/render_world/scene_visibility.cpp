#include "render/render_world/scene_visibility.h"

#include <utility>

#include "data/mesh.h"
#include "render/render_world/frustum.h"
#include "render/render_resource_resolver.h"
#include "spatial/aabb.h"

namespace kpengine::render
{
    namespace
    {
        MeshProxy MakeSectionPacketProxy(const MeshProxy &source,
                                         MaterialInstanceHandle material,
                                         const spatial::AABB &world_bounds)
        {
            // VisibleMeshSection is a value packet, not another owner of the
            // RenderWorld material-slot array. Keep only data consumed by
            // filtering and recording so section count cannot multiply heap
            // allocations for section_materials.
            MeshProxy packet{};
            packet.handle = source.handle;
            packet.mesh = source.mesh;
            packet.material = material;
            packet.world_transform = source.world_transform;
            packet.world_bounds = world_bounds;
            packet.flags = source.flags;
            packet.lod_bias = source.lod_bias;
            return packet;
        }
    }

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

    namespace
    {
        std::vector<VisibleMeshSection> BuildSections(
            const std::vector<MeshProxy> &proxies,
            const RenderResourceResolver &resource_resolver,
            const Frustum *const frustum)
        {
            std::vector<VisibleMeshSection> result;
            result.reserve(proxies.size());
            for (const MeshProxy &proxy : proxies)
            {
                if (!proxy.flags.visible ||
                    (frustum != nullptr && frustum->Intersects(proxy.world_bounds) == false))
                {
                    continue;
                }

                const std::vector<data::MeshSection> *const sections =
                    resource_resolver.FindMeshSections(proxy.mesh);
                if (sections == nullptr || sections->empty())
                {
                    result.push_back({MakeSectionPacketProxy(proxy, proxy.material,
                                                             proxy.world_bounds),
                                      proxy.world_bounds,
                                      std::numeric_limits<uint32_t>::max()});
                    continue;
                }

                for (std::size_t section_index = 0; section_index < sections->size();
                     ++section_index)
                {
                    const data::MeshSection &section = (*sections)[section_index];
                    if (section.index_count == 0 ||
                        section_index > std::numeric_limits<uint32_t>::max())
                    {
                        continue;
                    }
                    spatial::AABB world_bounds =
                        spatial::TransformAABB(section.local_bounds, proxy.world_transform);
                    if (!world_bounds.IsValid())
                    {
                        world_bounds = proxy.world_bounds;
                    }
                    if (frustum != nullptr && !frustum->Intersects(world_bounds))
                    {
                        continue;
                    }
                    MeshProxy section_proxy = MakeSectionPacketProxy(
                        proxy, proxy.GetMaterialForSection(section.material_index), world_bounds);
                    result.push_back({std::move(section_proxy), world_bounds,
                                      static_cast<uint32_t>(section_index)});
                }
            }
            return result;
        }
    }

    std::vector<VisibleMeshSection> SceneVisibility::BuildVisibleSections(
        const Matrix4f &view_projection, const std::vector<MeshProxy> &proxies,
        const RenderResourceResolver &resource_resolver)
    {
        const Frustum frustum = Frustum::FromViewProjection(view_projection);
        return BuildSections(proxies, resource_resolver, &frustum);
    }

    std::vector<VisibleMeshSection> SceneVisibility::BuildSectionCandidates(
        const std::vector<MeshProxy> &proxies,
        const RenderResourceResolver &resource_resolver)
    {
        return BuildSections(proxies, resource_resolver, nullptr);
    }

    std::vector<VisibleMeshSection> SceneVisibility::FilterVisibleSections(
        const Matrix4f &view_projection,
        const std::vector<VisibleMeshSection> &section_packets)
    {
        const Frustum frustum = Frustum::FromViewProjection(view_projection);
        std::vector<VisibleMeshSection> visible;
        visible.reserve(section_packets.size());
        for (const VisibleMeshSection &packet : section_packets)
        {
            if (frustum.Intersects(packet.world_bounds))
            {
                visible.push_back(packet);
            }
        }
        return visible;
    }
}
