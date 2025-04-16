#include "mesh_scene_proxy.h"

#include <glad/glad.h>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/core/utility/gl_vertex_array_guard.h"

namespace kpengine{
    MeshSceneProxy::MeshSceneProxy():
    vao_(0),
    mesh_sections_({}),
    current_shader_(nullptr)
    {}
    void MeshSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {
        GlVertexArrayGuard vao_guard(vao_);

        bool is_use_material_shader = shader != nullptr;
        Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(transfrom_);
        
        if(false == is_use_material_shader)
        {
            shader->UseProgram();
            for(std::vector<MeshSection>::iterator iter = mesh_sections_.begin(); iter != mesh_sections_.end(); iter++)
            {
                shader->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(iter->face_count * sizeof(unsigned int)));
            }
        }
        else
        {
            for(std::vector<MeshSection>::iterator iter = mesh_sections_.begin(); iter != mesh_sections_.end(); iter++)
            {
                std::shared_ptr<RenderShader> new_shader = iter->material->shader_;
                if(new_shader != current_shader_)
                {
                    current_shader_ = new_shader;
                    current_shader_->UseProgram();
                    current_shader_->SetMat(SHADER_PARAM_MODEL_TRANSFORM, transform_mat[0]);
                }
                //set transform
                glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(iter->face_count * sizeof(unsigned int)));
            }
        }

    }

    void MeshSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();
        for(std::vector<MeshSection>::iterator iter = mesh_sections_.begin(); iter != mesh_sections_.end(); iter++)
        {
            unsigned int shader_id = iter->material->shader_->GetShaderProgram();

            unsigned int uniform_block_index = glGetUniformBlockIndex(shader_id, "CameraMatrices");
            glUniformBlockBinding(shader_id, uniform_block_index, 0);

            unsigned int light_block_index = glGetUniformBlockIndex(shader_id, "Light");
            glUniformBlockBinding(shader_id, light_block_index, 1);

            std::shared_ptr<RenderShader> new_shader = iter->material->shader_;
            if(new_shader != current_shader_)
            {
                current_shader_ = new_shader;
                current_shader_->UseProgram();
                iter->material->Render(current_shader_.get());
            }
        }
    }
}