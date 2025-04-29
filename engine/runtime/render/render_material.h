#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_H

#include <memory>
#include <vector>
#include "runtime/core/math/math_header.h"
namespace kpengine{

    class RenderTexture; 
    class RenderShader;

    enum TextureSlots{
        DIFFUSE_0 = 1,
        DIFFUSE_1 = 2,
        DIFFUSE_2 = 3,
        SPECULAR_0 = 4,
        SPECULAR_1 = 5,
        SPECULAR_2 = 6,
        NORMAL = 7,
        EMISSION = 8
    };

    class RenderMaterial{
    
    public:
        void Initialize();
        void Render() ;
    public:
        std::vector<std::shared_ptr<RenderTexture>> diffuse_textures_;
        std::vector<std::shared_ptr<RenderTexture>>  specular_textures_;
        std::shared_ptr<RenderTexture> emission_texture_;
        std::shared_ptr<RenderTexture> normal_texture_;
        Vector3f diffuse_albedo_{1.f};
        float shininess = 70.f;
        bool normal_texture_enable_ = false;
        const unsigned int MAX_DIFFUSE_NUM = 3;
        const unsigned int MAX_SPECULAR_NUM = 3;
        std::shared_ptr<RenderShader> shader_;
    };



}

#endif