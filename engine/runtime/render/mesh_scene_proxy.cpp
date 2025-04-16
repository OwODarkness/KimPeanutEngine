#include "mesh_scene_proxy.h"

#include <glad/glad.h>
#include <iostream>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/core/log/logger.h"
namespace kpengine{
    MeshSceneProxy::MeshSceneProxy():
    vao_(0),
    mesh_sections_({}),
    current_shader_id_(0)
    {}
    void MeshSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {
        GlVertexArrayGuard vao_guard(vao_);

        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_);
        if(shader)
        {

            shader->UseProgram();
            for(std::vector<MeshSection>::iterator iter = mesh_sections_.begin(); iter != mesh_sections_.end(); iter++)
            {
                shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(0));
            }
        }
        else
        {
            for(std::vector<MeshSection>::iterator iter = mesh_sections_.begin(); iter != mesh_sections_.end(); iter++)
            {
                    unsigned int new_shader_id = iter->material->shader_->GetShaderProgram();
                if(new_shader_id != current_shader_id_)
                {
                    std::shared_ptr<RenderShader> current_shader = iter->material->shader_;

                    current_shader_id_ = new_shader_id;
                    current_shader->UseProgram();
                    current_shader->SetVec3("view_position", view_pos_);
                    current_shader->SetMat("light_space_matrix", light_space_);

                    current_shader->SetInt("shadow_map", 15);

                    current_shader->SetFloat("far_plane", 25.f);
                    current_shader->SetInt("point_shadow_map", 14);
                    iter->material->Render(current_shader.get());

                    current_shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                    
                }
                //set transform
                glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(0));
            }
            
        }

        current_shader_id_ = 0;
    }

    void MeshSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();
        for(std::vector<MeshSection>::iterator iter = mesh_sections_.begin(); iter != mesh_sections_.end(); iter++)
        {
            //Debug

            unsigned int shader_id = iter->material->shader_->GetShaderProgram();

            unsigned int uniform_block_index = glGetUniformBlockIndex(shader_id, "CameraMatrices");
            glUniformBlockBinding(shader_id, uniform_block_index, 0);

            unsigned int light_block_index = glGetUniformBlockIndex(shader_id, "Light");
            glUniformBlockBinding(shader_id, light_block_index, 1);

            unsigned int new_shader_id = iter->material->shader_->GetShaderProgram();
            if(new_shader_id != current_shader_id_)
            {
                current_shader_id_ = new_shader_id;
                std::shared_ptr<RenderShader> current_shader = iter->material->shader_;
                current_shader->UseProgram();
                iter->material->Render(current_shader.get());
            }

        }
        current_shader_id_ = 0;
    }
}