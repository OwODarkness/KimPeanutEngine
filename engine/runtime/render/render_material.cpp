#include "render_material.h"

#include <glad/glad.h>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"

namespace kpengine
{
    void RenderMaterialStanard::Render(RenderShader *shader_helper)
    {

        //shader_helper->SetFloat("material.diffuse", diffuse);
        //shader_helper->SetFloat("material.specular", specular);
        shader_helper->SetFloat("material.shininess", shininess);

        std::string texture_prefix = "";
        std::string texture_id = "";

        int diffuse_texture_num = diffuse_textures_.size();

        for (int i = 0; i < diffuse_texture_num; i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, diffuse_textures_[i]->GetTexture());
            texture_prefix = "material.diffuse_texture_";
            texture_id = std::to_string(i);
            shader_helper->SetInt(texture_prefix + texture_id, i);
        }

        for (int i = 0; i < specular_textures_.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + diffuse_texture_num + i);
            glBindTexture(GL_TEXTURE_2D, specular_textures_[i]->GetTexture());
            texture_prefix = "material.specular_texture_";
            texture_id = std::to_string(i);
            shader_helper->SetInt(texture_prefix + texture_id, diffuse_texture_num + i);
        }

        if(emmision_texture)
        {
            glActiveTexture(GL_TEXTURE0 + diffuse_texture_num + specular_textures_.size());
            glBindTexture(GL_TEXTURE_2D, emmision_texture->GetTexture());
            shader_helper->SetInt("material.emission_texture", diffuse_texture_num + specular_textures_.size());
        }
    }

    void RenderMaterialSkyBox::Render(RenderShader* shader_helper)
    {
        shader_helper->SetInt("skybox", cube_map_texture_->GetTexture());
        glActiveTexture(GL_TEXTURE0 + cube_map_texture_->GetTexture());
        glBindTexture(GL_TEXTURE_CUBE_MAP, cube_map_texture_->GetTexture());
    }
}