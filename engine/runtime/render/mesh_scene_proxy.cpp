#include "mesh_scene_proxy.h"

#include <glad/glad.h>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"

namespace kpengine{
    void MeshSceneProxy::Draw(std::shared_ptr<RenderShader> shader)
    {
        bool is_use_material_shader = shader != nullptr;
        glBindVertexArray(vao_);
        if(false == is_use_material_shader)
        {
            shader->UseProgram();
        }
        std::vector<MeshSection>::const_iterator iter;
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
}