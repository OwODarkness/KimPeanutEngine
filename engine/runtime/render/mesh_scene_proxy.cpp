#include "mesh_scene_proxy.h"

#include <glad/glad.h>

#include "runtime/render/render_mesh_resource.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/core/log/logger.h"
namespace kpengine{
    MeshSceneProxy::MeshSceneProxy():
    vao_(0),
    mesh_resourece_ref_(nullptr),
    current_shader_id_(0)
    {}
    void MeshSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {
        GlVertexArrayGuard vao_guard(vao_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_).Transpose();
        if(shader)
        {
            shader->UseProgram();
            for(std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
            {
                shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                iter->material->Render(shader);
                glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(iter->index_start* sizeof(unsigned int)));
                
            }
        }
        else
        {

            for(std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
            {
                unsigned int new_shader_id = iter->material->shader_->GetShaderProgram();
                std::shared_ptr<RenderShader> current_shader = iter->material->shader_;
                
                current_shader->UseProgram();
                if(new_shader_id != current_shader_id_)
                {
                    current_shader_id_ = new_shader_id;
                    current_shader->SetVec3("view_position", view_pos_);
                    current_shader->SetMat("light_space_matrix", light_space_);
                    current_shader->SetInt("shadow_map", 15);
                    current_shader->SetFloat("far_plane", 25.f);
                    current_shader->SetInt("point_shadow_map", 14);
                    current_shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);


                }
                iter->material->Render(current_shader);

                current_shader->SetInt("irradiance_map", 10);
                glActiveTexture(GL_TEXTURE10);
                glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance_map_handle_);

                current_shader->SetInt("prefilter_map", 11);
                glActiveTexture(GL_TEXTURE11);
                glBindTexture(GL_TEXTURE_CUBE_MAP, prefilter_map_handle_);

                current_shader->SetInt("brdf_map", 12);
                glActiveTexture(GL_TEXTURE12);
                glBindTexture(GL_TEXTURE_2D, brdf_map_handle_);

                glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(iter->index_start * sizeof(unsigned int)));
            }
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        current_shader_id_ = 0;
    }

    void MeshSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();
        int count = 0;
        for(std::vector<MeshSection>::iterator iter = mesh_resourece_ref_->mesh_sections_.begin(); iter != mesh_resourece_ref_->mesh_sections_.end(); iter++)
        {
            //Debug
            std::shared_ptr<RenderShader> shader = iter->material->shader_;
            unsigned int shader_id = shader->GetShaderProgram();

            unsigned int uniform_block_index = glGetUniformBlockIndex(shader_id, "CameraMatrices");
            glUniformBlockBinding(shader_id, uniform_block_index, 0);

            unsigned int light_block_index = glGetUniformBlockIndex(shader_id, "Light");
            glUniformBlockBinding(shader_id, light_block_index, 1);


        }
        current_shader_id_ = 0;
    }
}