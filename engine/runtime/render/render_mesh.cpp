#include "render_mesh.h"

#include <glad/glad.h>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/render/render_mesh_resource.h"

#include "runtime/render/model_loader.h"

namespace kpengine
{
    RenderMesh::RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned> indices, std::shared_ptr<RenderMaterial> material) : verticles_(verticles), indices_(indices), material_(material)
    {
    }

    RenderMesh::~RenderMesh()
    {

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
        //glActiveTexture(GL_TEXTURE0);
    }

    RenderMeshStandard::~RenderMeshStandard()
    {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &ebo_);
        glDeleteBuffers(1, &vbo_);
    }


    RenderMesh_V2::RenderMesh_V2(){}

    RenderMesh_V2::RenderMesh_V2(const std::string& mesh_relative_path):
    name_(mesh_relative_path), lod_level(0){}

    const RenderMeshResource* RenderMesh_V2::GetMeshResource() const
    {
        //return lod_mesh_resources.at(lod_level).get();
        return mesh_resource.get();
    }
    void RenderMesh_V2::Initialize()
    {
        mesh_resource = std::make_unique<RenderMeshResource>();
        ModelLoader_V2::Load(name_, *mesh_resource);
        //lod_mesh_resources.push_back(mesh_resource);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);
        
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, mesh_resource->vertex_buffer_.size() * sizeof(MeshVertex), mesh_resource->vertex_buffer_.data(), GL_STATIC_DRAW );

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh_resource->index_buffer_.size() * sizeof(unsigned int), mesh_resource->index_buffer_.data(), GL_STATIC_DRAW);

        //position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)0);
        //normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(offsetof(MeshVertex, normal)));
        //tangent
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(offsetof(MeshVertex, tangent)));
        //tex_coord
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(offsetof(MeshVertex, tex_coord)));
        //color
    }

    RenderMesh_V2::~RenderMesh_V2()
    {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
    }
    
}