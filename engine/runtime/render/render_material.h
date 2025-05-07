#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_H

#include <memory>
#include <vector>
#include <string>
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

    constexpr unsigned int MAX_DIFFUSE_NUM = 3;
    constexpr unsigned int MAX_SPECULAR_NUM = 3;

    class RenderMaterial{
    
    public:
        static std::shared_ptr<RenderMaterial> CreateMaterial(const std::vector<std::string>& diffuse_textures_path_container, 
            const std::vector<std::string>& specular_textures_path_container, const std::string& emission_texture_path, const std::string normal_texture_path, const std::string shader_name);
            
        void Initialize();
        void Render() ;
    public:
        std::shared_ptr<RenderShader> shader_;
        
        std::vector<std::shared_ptr<RenderTexture>> diffuse_textures_;
        std::vector<std::shared_ptr<RenderTexture>>  specular_textures_;
        std::shared_ptr<RenderTexture> emission_texture_;
        std::shared_ptr<RenderTexture> normal_texture_;

        bool normal_texture_enable_ = false;

        float metallic = 0.5f;
        float roughness = 0.5f;
        float shininess = 70.f;
        float ao = 1.f;
        Vector3f albedo{0.5f, 0.f, 0.f};
        Vector3f diffuse_albedo_{1.f};
    };



}

#endif