#include "render_material.h"

#include <glad/glad.h>
#include <iostream>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"

namespace kpengine
{

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
        
    }

    void RenderMaterial::Render()
    {
        unsigned int diffuse_texture_num =  std::min((unsigned int)diffuse_textures_.size(), MAX_DIFFUSE_NUM);
        unsigned int specular_texture_num = std::min((unsigned int)specular_textures_.size(), MAX_SPECULAR_NUM);
        
        shader_->SetInt("material.diffuse_count", diffuse_texture_num);
        shader_->SetInt("material.specular_count", specular_texture_num);
        shader_->SetBool("normal_texture_enabled", normal_texture_enable_);

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