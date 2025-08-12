#ifndef KPENGINE_RUNTIME_RENDER_CONTEXT_H
#define KPENGINE_RUNTIME_RENDER_CONTEXT_H

#include <memory>
#include <array>
namespace kpengine
{
    class RenderShader;
    struct RenderContext
    {
        std::shared_ptr<RenderShader> shader;
        int light_num;
        float near_plane;
        float far_plane;
        float* view_position;
        float* light_space_matrix;
        unsigned int directional_shadow_map;
        std::array<unsigned int, 4> point_shadow_map;
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