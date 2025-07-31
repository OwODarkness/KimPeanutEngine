#ifndef KPENGINE_RUNTIME_RENDER_CONTEXT_H
#define KPENGINE_RUNTIME_RENDER_CONTEXT_H

#include <memory>

namespace kpengine
{
    class RenderShader;
    struct RenderContext
    {
        std::shared_ptr<RenderShader> shader;
        unsigned int directional_shadow_map;
        unsigned int point_shadow_map;
        unsigned int irradiance_map;
        unsigned int prefilter_map;
        unsigned int brdf_map;
    };
}

#endif