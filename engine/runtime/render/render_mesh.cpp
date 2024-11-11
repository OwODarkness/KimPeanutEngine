#include "render_mesh.h"

#include <glad/glad.h>
#include <iostream>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"

namespace kpengine
{
    RenderMesh::RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned> indices, std::shared_ptr<RenderMaterial> material) : verticles_(verticles), indices_(indices), material_(material)
    {
    }

    RenderMesh::~RenderMesh()
    {
        glDeleteBuffers(1, &vao_);
        glDeleteBuffers(1, &vbo_);
    }

    void RenderMesh::Initialize(std::shared_ptr<RenderShader> shader_helper)
    {

        assert(shader_helper);
        shader_helper_ = shader_helper;

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }

    void RenderMesh::Draw()
    {
    }

    RenderMeshStandard::RenderMeshStandard(std::vector<Vertex> verticles, std::vector<unsigned int> indices, std::shared_ptr<RenderMaterial> material) : RenderMesh(verticles, indices, material)
    {
    }

    void RenderMeshStandard::Initialize(std::shared_ptr<RenderShader> shader_helper)
    {

        RenderMesh::Initialize(shader_helper);

        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verticles_.size() * sizeof(Vertex), &verticles_[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(0));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, tex_coord)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void RenderMeshStandard::Draw()
    {
        material_->Render(shader_helper_.get());
        glBindVertexArray(vao_);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    RenderMeshStandard::~RenderMeshStandard()
    {
        glDeleteBuffers(1, &ebo_);
    }

    SkyBox::SkyBox(std::shared_ptr<RenderMaterial> material) : RenderMesh({{{-1, 1, -1}}, {{-1, -1, -1}}, {{1, -1, -1}}, {{1, -1, -1}}, {{1, 1, -1}}, {{-1, 1, -1}}, {{-1, -1, 1}}, {{-1, -1, -1}}, {{-1, 1, -1}}, {{-1, 1, -1}}, {{-1, 1, 1}}, {{-1, -1, 1}}, {{1, -1, -1}}, {{1, -1, 1}}, {{1, 1, 1}}, {{1, 1, 1}}, {{1, 1, -1}}, {{1, -1, -1}}, {{-1, -1, 1}}, {{-1, 1, 1}}, {{1, 1, 1}}, {{1, 1, 1}}, {{1, -1, 1}}, {{-1, -1, 1}}, {{-1, 1, -1}}, {{1, 1, -1}}, {{1, 1, 1}}, {{1, 1, 1}}, {{-1, 1, 1}}, {{-1, 1, -1}}, {{-1, -1, -1}}, {{-1, -1, 1}}, {{1, -1, -1}}, {{1, -1, -1}}, {{-1, -1, 1}}, {{1, -1, 1}}},
                                                                          {}, material)
    {
    }

    void SkyBox::Initialize(std::shared_ptr<RenderShader> shader_helper)
    {
        RenderMesh::Initialize(shader_helper);
        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verticles_.size() * sizeof(Vertex), &verticles_[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(0));

        glBindVertexArray(0);
    }

    void SkyBox::Draw()
    {
        glDepthMask(GL_FALSE);
        material_->Render(shader_helper_.get());
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
    }
}