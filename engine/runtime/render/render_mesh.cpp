#include "render_mesh.h"

#include <glad/glad.h>
#include <iostream>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"

namespace kpengine
{
        float skyboxVertices[] = {
            // positions
            -1.0f, 1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,

            -1.0f, -1.0f, 1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 1.0f,

            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 1.0f,
            -1.0f, -1.0f, 1.0f,

            -1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, 1.0f};

    RenderMesh::RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned> indices, std::shared_ptr<RenderMaterial> material) : verticles_(verticles), indices_(indices), material_(material)
    {
    }

    RenderMesh::~RenderMesh()
    {
        glDeleteBuffers(1, &vao_);
        glDeleteBuffers(1, &vbo_);
    }

    void RenderMesh::Initialize()
    {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }

    void RenderMesh::Draw(std::shared_ptr<RenderShader> shader_helper)
    {
    }


    RenderMeshStandard::RenderMeshStandard(std::vector<Vertex> verticles, std::vector<unsigned int> indices, std::shared_ptr<RenderMaterial> material) : RenderMesh(verticles, indices, material)
    {
    }

    void RenderMeshStandard::Initialize()
    {

        RenderMesh::Initialize();

        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verticles_.size() * sizeof(Vertex), &verticles_[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(0));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, normal)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, tex_coord)));

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, tangent)));


        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void RenderMeshStandard::Draw(std::shared_ptr<RenderShader> shader_helper)
    {
        material_->Render(shader_helper.get());
        glBindVertexArray(vao_);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
         glActiveTexture(GL_TEXTURE0);
    }

    RenderMeshStandard::~RenderMeshStandard()
    {
        glDeleteBuffers(1, &ebo_);
    }

    SkyBox::SkyBox(std::shared_ptr<RenderMaterial> material) : RenderMesh({}, {}, material)
    {
    }

    void SkyBox::Initialize()
    {


        RenderMesh::Initialize();
        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)(0));

        glBindVertexArray(0);
    }

    void SkyBox::Draw(std::shared_ptr<RenderShader> shader_helper)
    {
        glDepthFunc(GL_LEQUAL);

        glBindVertexArray(vao_);
        material_->Render(shader_helper.get());
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        glDepthFunc(GL_LESS);
         glActiveTexture(GL_TEXTURE0);

    }
}