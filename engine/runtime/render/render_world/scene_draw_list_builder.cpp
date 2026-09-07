#include "render/render_world/scene_draw_list.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "data/mesh.h"

#include "render/render_resource_resolver.h"

namespace kpengine::render
{
    namespace
    {
        bool SupportsPass(const MaterialTemplateDesc &desc, MaterialPass pass)
        {
            return std::find(desc.compatible_passes.begin(), desc.compatible_passes.end(), pass) !=
                   desc.compatible_passes.end();
        }
    }

    SceneDrawLists SceneDrawListBuilder::Build(
        const std::vector<MeshProxy> &visible_proxies, const MaterialSystem &materials,
        const RenderResourceResolver &resource_resolver, MaterialPass pass)
    {
        SceneDrawLists draw_lists;
        draw_lists.opaque.reserve(visible_proxies.size());
        draw_lists.alpha_blend.reserve(visible_proxies.size());

        const auto append_item = [&](const MeshProxy &proxy, uint32_t section_index)
        {
            if (!proxy.material.IsValid() ||
                materials.GetInstanceResolution(proxy.material).state != MaterialResourceState::Ready)
            {
                return;
            }

            const MaterialTemplateHandle template_handle =
                materials.GetInstanceTemplate(proxy.material);
            const MaterialTemplateDesc *const template_desc = materials.FindTemplate(template_handle);
            const graphics::PipelineHandle pipeline =
                resource_resolver.FindMaterialPipeline(template_handle, pass);
            const std::optional<MaterialDrawClass> draw_class =
                materials.GetDrawClass(proxy.material);
            if (!template_desc || !SupportsPass(*template_desc, pass) ||
                !pipeline.IsValid() || !draw_class)
            {
                return;
            }

            SceneDrawItem item{proxy, pipeline, section_index};
            if (*draw_class == MaterialDrawClass::Opaque)
            {
                draw_lists.opaque.push_back(std::move(item));
            }
            else
            {
                draw_lists.alpha_blend.push_back(std::move(item));
            }
        };

        for (const MeshProxy &proxy : visible_proxies)
        {
            if (!proxy.mesh.IsValid())
            {
                continue;
            }

            const std::vector<data::MeshSection> *const sections =
                resource_resolver.FindMeshSections(proxy.mesh);
            if (sections == nullptr || sections->empty())
            {
                append_item(proxy, std::numeric_limits<uint32_t>::max());
                continue;
            }

            for (std::size_t section_index = 0; section_index < sections->size(); ++section_index)
            {
                const data::MeshSection &section = (*sections)[section_index];
                if (section.index_count == 0 ||
                    section_index > std::numeric_limits<uint32_t>::max())
                {
                    continue;
                }
                MeshProxy section_proxy = proxy;
                section_proxy.material = proxy.GetMaterialForSection(section.material_index);
                section_proxy.section_materials.clear();
                append_item(section_proxy, static_cast<uint32_t>(section_index));
            }
        }
        SortOpaque(draw_lists.opaque);
        return draw_lists;
    }

    SceneDrawLists SceneDrawListBuilder::Build(
        const std::vector<VisibleMeshSection> &visible_sections,
        const MaterialSystem &materials, const RenderResourceResolver &resource_resolver,
        MaterialPass pass)
    {
        SceneDrawLists draw_lists;
        draw_lists.opaque.reserve(visible_sections.size());
        draw_lists.alpha_blend.reserve(visible_sections.size());

        for (const VisibleMeshSection &visible_section : visible_sections)
        {
            const MeshProxy &proxy = visible_section.proxy;
            if (!proxy.mesh.IsValid() || !proxy.material.IsValid() ||
                materials.GetInstanceResolution(proxy.material).state != MaterialResourceState::Ready)
            {
                continue;
            }
            const MaterialTemplateHandle template_handle =
                materials.GetInstanceTemplate(proxy.material);
            const MaterialTemplateDesc *const template_desc =
                materials.FindTemplate(template_handle);
            const graphics::PipelineHandle pipeline =
                resource_resolver.FindMaterialPipeline(template_handle, pass);
            const std::optional<MaterialDrawClass> draw_class =
                materials.GetDrawClass(proxy.material);
            if (!template_desc || !SupportsPass(*template_desc, pass) ||
                !pipeline.IsValid() || !draw_class)
            {
                continue;
            }
            SceneDrawItem item{proxy, pipeline, visible_section.section_index};
            if (*draw_class == MaterialDrawClass::Opaque)
            {
                draw_lists.opaque.push_back(std::move(item));
            }
            else
            {
                draw_lists.alpha_blend.push_back(std::move(item));
            }
        }
        SortOpaque(draw_lists.opaque);
        return draw_lists;
    }
}
