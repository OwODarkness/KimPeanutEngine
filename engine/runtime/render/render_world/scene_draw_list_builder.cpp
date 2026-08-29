#include "render/render_world/scene_draw_list.h"

#include <algorithm>

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
        for (const MeshProxy &proxy : visible_proxies)
        {
            if (!proxy.mesh.IsValid() ||
                materials.GetInstanceResolution(proxy.material).state != MaterialResourceState::Ready)
            {
                continue;
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
                continue;
            }

            SceneDrawItem item{proxy, pipeline};
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
