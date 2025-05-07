#include "render_material.h"

#include <glad/glad.h>
#include <iostream>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"
#include "runtime/runtime_header.h"
#include "runtime/render/texture_pool.h"
namespace kpengine
{

    std::shared_ptr<RenderMaterial> RenderMaterial::CreateMaterial(
        const std::vector<std::string>& diffuse_textures_path_container, 
        const std::vector<std::string>& specular_textures_path_container, 
        const std::string& emission_texture_path, 
        const std::string normal_texture_path, 
        const std::string shader_name)
    {
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();
        material->shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(shader_name);
        TexturePool* texture_pool = runtime::global_runtime_context.render_system_->GetTexturePool();
        
        //diffuse texture load
        for(const std::string &diffuse_path : diffuse_textures_path_container)
        {
            if(diffuse_path == "")
            {
                continue;
            }
            std::shared_ptr<RenderTexture> diffuse_texture = texture_pool->FetchTexture2D(diffuse_path);
            if(diffuse_texture)
            {
                material->diffuse_textures_.push_back(diffuse_texture);
            }
        }

        //specular texture load
        for(const std::string &specular_path : specular_textures_path_container)
        {
            if(specular_path == "")
            {
                continue;
            }
            std::shared_ptr<RenderTexture> specular_texture = texture_pool->FetchTexture2D(specular_path);
            if(specular_texture)
            {
                material->specular_textures_.push_back(specular_texture);
            }
        }

        if(emission_texture_path != "")
        {
            material->emission_texture_ = texture_pool->FetchTexture2D(emission_texture_path);
        }

        if(normal_texture_path != "")
        {
            material->normal_texture_ = texture_pool->FetchTexture2D(normal_texture_path);
        }

        return material;
    }

    void RenderMaterial::Initialize()
    {
        shader_->UseProgram();
        shader_->SetFloat("material.shininess", shininess);
        shader_->SetVec3("material.diffuse_albedo", diffuse_albedo_.Data());

        shader_->SetInt("material.diffuse_texture_0", TextureSlots::DIFFUSE_0);
        shader_->SetInt("material.diffuse_texture_1", TextureSlots::DIFFUSE_1);
        shader_->SetInt("material.diffuse_texture_2", TextureSlots::DIFFUSE_2);

        shader_->SetInt("material.specular_texture_0", TextureSlots::SPECULAR_0);
        shader_->SetInt("material.specular_texture_1", TextureSlots::SPECULAR_1);
        shader_->SetInt("material.specular_texture_2", TextureSlots::SPECULAR_2);

        shader_->SetInt("material.normal_texture", TextureSlots::NORMAL);
        shader_->SetInt("material.emission_texture", TextureSlots::EMISSION);
       
        unsigned int diffuse_texture_num =  std::min((unsigned int)diffuse_textures_.size(), MAX_DIFFUSE_NUM);
        unsigned int specular_texture_num = std::min((unsigned int)specular_textures_.size(), MAX_SPECULAR_NUM);
        
        shader_->SetInt("material.diffuse_count", diffuse_texture_num);
        shader_->SetInt("material.specular_count", specular_texture_num);
        shader_->SetBool("normal_texture_enabled", normal_texture_enable_);
        shader_->SetFloat("material.ao", ao);

        shader_->SetVec3("material.albedo", albedo.Data());
    }

    void RenderMaterial::Render()
    {
        unsigned int diffuse_texture_num =  std::min((unsigned int)diffuse_textures_.size(), MAX_DIFFUSE_NUM);
        unsigned int specular_texture_num = std::min((unsigned int)specular_textures_.size(), MAX_SPECULAR_NUM);
        
        shader_->SetInt("material.diffuse_count", diffuse_texture_num);
        shader_->SetInt("material.specular_count", specular_texture_num);
        shader_->SetBool("normal_texture_enabled", normal_texture_enable_);
        
        shader_->SetFloat("material.metallic", metallic);
        shader_->SetFloat("material.roughness", roughness);

        
        for (unsigned int i = 0; i < diffuse_texture_num; i++)
        {
            glActiveTexture(GL_TEXTURE0 + i + TextureSlots::DIFFUSE_0);
            glBindTexture(GL_TEXTURE_2D, diffuse_textures_[i]->GetTexture());
        }
        for (unsigned int i = 0; i < specular_texture_num; i++)
        {
            glActiveTexture(GL_TEXTURE0 + i + TextureSlots::SPECULAR_0);
            glBindTexture(GL_TEXTURE_2D, specular_textures_[i]->GetTexture());

        }
        if(emission_texture_)
        {
            glActiveTexture(GL_TEXTURE0 + TextureSlots::EMISSION);
            glBindTexture(GL_TEXTURE_2D, emission_texture_->GetTexture());
        }
        if(normal_texture_ )
        {
            glActiveTexture(GL_TEXTURE0 + TextureSlots::NORMAL);
            glBindTexture(GL_TEXTURE_2D, normal_texture_->GetTexture());
        }

    }


}