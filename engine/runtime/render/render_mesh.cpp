#include "render_mesh.h"

#include <glad/glad.h>
#include <iostream>


#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
namespace kpengine{
   RenderMesh::RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned> indices, std::shared_ptr<RenderMaterial> material):
    verticles_(verticles),indices_(indices), material_(material)
    {
    }

    RenderMesh::~RenderMesh()
    {
        
    }

    void RenderMesh::Initialize(std::shared_ptr<RenderShader> shader_helper)
    {
        assert(shader_helper);
        shader_helper_ = shader_helper;

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verticles_.size() * sizeof(Vertex) , &verticles_[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(2); 
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),  (void*)(offsetof(Vertex, tex_coord)));

        glBindVertexArray(0);

    }

    void RenderMesh::Draw()
    {
        glBindVertexArray(vao_);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, 0);

        shader_helper_->SetFloat("material.diffuse", material_->diffuse);
        shader_helper_->SetFloat("material.specular", material_->specular);
        int count = 0;
        std::string texture_prefix = "";
        std::string texture_id = "";
        for(int i = 0;i<material_->diffuse_textures_.size();i++)
        {
            glActiveTexture(GL_TEXTURE0 + count);
            glBindTexture(GL_TEXTURE_2D, material_->diffuse_textures_[i]->GetTexture());
            texture_prefix = "material.diffuse_texture_";
            texture_id = std::to_string(count);           
            shader_helper_->SetInt( texture_prefix + texture_id, count);
            count++;
        }

        for(int i = 0;i<material_->specular_textures_.size();i++)
        {
            glActiveTexture(GL_TEXTURE0 + count);
            glBindTexture(GL_TEXTURE_2D, material_->specular_textures_[i]->GetTexture());
            texture_prefix = "material.specular_texture_";
            texture_id = std::to_string(count);           
            shader_helper_->SetInt( texture_prefix + texture_id, count);
            count++;
        }

        glBindVertexArray(0);

    }
}