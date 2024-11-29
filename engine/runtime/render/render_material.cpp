#include "render_material.h"

#include <glad/glad.h>
#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"

namespace kpengine
{
    void RenderMaterialStanard::Render(RenderShader *shader_helper)
    {


        shader_helper->SetFloat("material.shininess", shininess);

        std::string texture_prefix = "";
        std::string texture_id = "";

        int diffuse_texture_num = diffuse_textures_.size();

        for (int i = 0; i < diffuse_texture_num; i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            texture_prefix = "material.diffuse_texture_";
            texture_id = std::to_string(i);
            shader_helper->SetInt(texture_prefix + texture_id, i);
            glBindTexture(GL_TEXTURE_2D, diffuse_textures_[i]->GetTexture());

        }
        int specular_texture_num = specular_textures_.size();
        for (int i = 0; i < specular_texture_num; i++)
        {
            glActiveTexture(GL_TEXTURE0 + diffuse_texture_num + i);
            texture_prefix = "material.specular_texture_";
            texture_id = std::to_string(i);
            shader_helper->SetInt(texture_prefix + texture_id, diffuse_texture_num + i);
            glBindTexture(GL_TEXTURE_2D, specular_textures_[i]->GetTexture());

        }
        if(emmision_texture)
        {
            glActiveTexture(GL_TEXTURE0 + diffuse_texture_num + specular_texture_num);
            shader_helper->SetInt("material.emission_texture", diffuse_texture_num + specular_texture_num);
            glBindTexture(GL_TEXTURE_2D, emmision_texture->GetTexture());
        }
        if(normal_texture_ && normal_texture_enable_)
        {
            glActiveTexture(GL_TEXTURE0 + diffuse_texture_num + specular_texture_num + 1);
            shader_helper->SetInt("material.normal_texture", diffuse_texture_num + specular_texture_num + 1);
            glBindTexture(GL_TEXTURE_2D, normal_texture_->GetTexture());
            shader_helper->SetBool("normal_texture_enabled", normal_texture_enable_);
        }
    }

    void RenderMaterialSkyBox::Render(RenderShader* shader_helper)
    {
         glActiveTexture(GL_TEXTURE0 );
        // shader_helper->SetInt("skybox", 0);
         glBindTexture(GL_TEXTURE_CUBE_MAP, cube_map_texture_->GetTexture());
    }
}