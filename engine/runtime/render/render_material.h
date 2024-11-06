#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_H

#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace kpengine{

    class RenderTexture; 

    class RenderMaterial{

    public:
        std::vector<std::shared_ptr<RenderTexture>> diffuse_textures_;
        std::vector<std::shared_ptr<RenderTexture>>  specular_textures_;
        float diffuse{0.5f};
        float specular{1.f};
        float shiness = 32.f;
    };
}

#endif