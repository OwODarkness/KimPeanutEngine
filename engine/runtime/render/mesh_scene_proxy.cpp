#include "mesh_scene_proxy.h"

#include <glad/glad.h>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"

namespace kpengine{
    MeshSceneProxy::MeshSceneProxy():
    vao_(0),
    mesh_sections({})
    {}
    void MeshSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {
        bool is_use_material_shader = shader != nullptr;
        glBindVertexArray(vao_);
        if(false == is_use_material_shader)
        {
            shader->UseProgram();
        }
        std::vector<MeshSection>::iterator iter;
        for(iter = mesh_sections.begin(); iter != mesh_sections.end(); iter++)
        {
            //TODO: use section material shader
            if(true == is_use_material_shader)
            {
                iter->material->shader_->UseProgram();
            }
            //set transform
            glDrawElements(GL_TRIANGLES, iter->index_count, GL_UNSIGNED_INT, (void*)(iter->face_count * sizeof(unsigned int)));
        }
        glBindVertexArray(0);
    }

    void MeshSceneProxy::Initialize()
    {
        PrimitiveSceneProxy::Initialize();
        std::vector<MeshSection>::iterator iter;
        for(iter = mesh_sections.begin(); iter != mesh_sections.end(); iter++)
        {
            unsigned int shader_id = iter->material->shader_->GetShaderProgram();

            unsigned int uniform_block_index = glGetUniformBlockIndex(shader_id, "CameraMatrices");
            glUniformBlockBinding(shader_id, uniform_block_index, 0);

            unsigned int light_block_index = glGetUniformBlockIndex(shader_id, "Light");
            glUniformBlockBinding(shader_id, light_block_index, 1);
        }
    }
}