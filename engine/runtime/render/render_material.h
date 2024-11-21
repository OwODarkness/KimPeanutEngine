#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_H

#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace kpengine{

    class RenderTexture; 
    class RenderShader;

    class RenderMaterial{

    public:
        virtual void Render(RenderShader* shader_helper) = 0;
    };

    class RenderMaterialStanard : public RenderMaterial{
        public:
        virtual void Render(RenderShader* shader_helper) override;
    
            public:
        std::vector<std::shared_ptr<RenderTexture>> diffuse_textures_;
        std::vector<std::shared_ptr<RenderTexture>>  specular_textures_;
        std::shared_ptr<RenderTexture> emmision_texture;
        float kd{0.5f};
        float ks{1.f};
        float shininess = 70.f;
    };

    class RenderMaterialSkyBox: public RenderMaterial{
    public:
        std::shared_ptr<RenderTexture> cube_map_texture_;
    
        virtual void Render(RenderShader* shader_helper) override;
    
    };
}

#endif