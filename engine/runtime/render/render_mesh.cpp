#include "render_mesh.h"

#include <glad/glad.h>
#include "runtime/core/log/logger.h"

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
    name_(mesh_relative_path), lod_level(0), mesh_resource(std::make_unique<RenderMeshResource>())
    {}



    void RenderMesh::Initialize()
    {
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
    
        is_initialized_ = true;
    }

    void  RenderMesh::SetMaterial(const std::shared_ptr<RenderMaterial>& material, unsigned int section_index)
    {

        bool is_index_valid = section_index >=0 && section_index < mesh_resource->mesh_sections_.size();
        if(!is_index_valid)
        {
            KP_LOG("MeshLog", LOG_LEVEL_ERROR, "try to access(set material) invalid mesh section with index: %d", section_index);
            return;
        }
        mesh_resource->mesh_sections_.at(section_index).material = material;
        if(is_initialized_)
        {
            mesh_resource->mesh_sections_.at(section_index).material->Initialize();
        }
    }

    std::shared_ptr<RenderMaterial> RenderMesh::GetMaterial(unsigned int section_index)
    {
        bool is_index_valid = section_index >=0 && section_index < mesh_resource->mesh_sections_.size();
        if(!is_index_valid)
        {
            KP_LOG("MeshLog", LOG_LEVEL_ERROR, "try to access(get material) invalid mesh section with index: %d", section_index);
            return nullptr;
        }

        return mesh_resource->mesh_sections_.at(section_index).material;

    }

    RenderMesh::~RenderMesh()
    {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
    }
    
}