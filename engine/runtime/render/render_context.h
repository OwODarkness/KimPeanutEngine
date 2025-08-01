#ifndef KPENGINE_RUNTIME_RENDER_CONTEXT_H
#define KPENGINE_RUNTIME_RENDER_CONTEXT_H

#include <memory>

namespace kpengine
{
    class RenderShader;
    struct RenderContext
    {
        std::shared_ptr<RenderShader> shader;
        float far_plane;
        float* view_position;
        unsigned int directional_shadow_map;
        unsigned int point_shadow_map;
        unsigned int irradiance_map;
        unsigned int prefilter_map;
        unsigned int brdf_map;
        unsigned int g_position;
        unsigned int g_normal;
        unsigned int g_albedo;
        unsigned int g_material;
    };
}

#endif