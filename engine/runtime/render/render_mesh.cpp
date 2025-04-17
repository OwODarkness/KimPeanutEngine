#include "render_mesh.h"

#include <glad/glad.h>
#include <iostream>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/render/render_mesh_resource.h"

#include "runtime/render/model_loader.h"

#include "runtime/core/utility/utility.h"

namespace kpengine
{
    RenderMesh::RenderMesh(){}

    RenderMesh::RenderMesh(const std::string& mesh_relative_path):
    name_(mesh_relative_path), lod_level(0){}

    const RenderMeshResource* RenderMesh::GetMeshResource() const
    {
        //return lod_mesh_resources.at(lod_level).get();
        return mesh_resource.get();
    }
    void RenderMesh::Initialize()
    {
        mesh_resource = std::make_unique<RenderMeshResource>();
        ModelLoader::Load(name_, *mesh_resource);

        //lod_mesh_resources.push_back(mesh_resource);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        GlVertexArrayGuard vao_guard(vao_);

        GlVertexBufferGuard vbo_guard(vbo_);
        glBufferData(GL_ARRAY_BUFFER, mesh_resource->vertex_buffer_.size() * sizeof(MeshVertex), mesh_resource->vertex_buffer_.data(), GL_STATIC_DRAW );

        GlElementBufferGuard ebo_guard(ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh_resource->index_buffer_.size() * sizeof(unsigned int), mesh_resource->index_buffer_.data(), GL_STATIC_DRAW);

        //position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)0);
        //normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(offsetof(MeshVertex, normal)));

        //tex_coord
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(offsetof(MeshVertex, tex_coord)));
        //tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(offsetof(MeshVertex, tangent)));
    
    }

    RenderMesh::~RenderMesh()
    {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
    }
    
}