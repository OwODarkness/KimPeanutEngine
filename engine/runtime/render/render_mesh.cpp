#include "render_mesh.h"

#include <iostream>
#include <glad/glad.h>
#include <meshoptimizer/meshoptimizer.h>
#include "runtime/core/log/logger.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/render/render_mesh_resource.h"
#include "runtime/render/aabb.h"
#include "runtime/render/model_loader.h"

#include "runtime/core/utility/utility.h"

namespace kpengine
{

    RenderMesh::RenderMesh() {}

    RenderMesh::RenderMesh(const std::string &mesh_relative_path) : name_(mesh_relative_path), lod_level(0), lod_max_level(0)
    {
    }

    void RenderMesh::Initialize()
    {
        lod_mesh_resources_.push_back(std::make_unique<RenderMeshResource>());
        mesh_resource_ = GetMeshResource(0);
        ModelLoader::Load(name_, *mesh_resource_);

        lod_max_level = CalculateLODCount(mesh_resource_->index_buffer_.size() / 3);
        geometry_buf_ = CreateGeometryBuffer(mesh_resource_);
        geometry_buffers_.push_back(geometry_buf_);

        for(unsigned int i = 1 ; i<=lod_max_level;i++)
        {
            BuildLODMeshResource(i);
        }

        is_initialized_ = true;
    }

    GeometryBuffer RenderMesh::CreateGeometryBuffer(RenderMeshResource *mesh_resource)
    {
        unsigned int vao;
        unsigned int vbo;
        unsigned int ebo;

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        GlVertexArrayGuard vao_guard(vao);

        GlVertexBufferGuard vbo_guard(vbo);
        glBufferData(GL_ARRAY_BUFFER, mesh_resource->vertex_buffer_.size() * sizeof(MeshVertex), mesh_resource->vertex_buffer_.data(), GL_STATIC_DRAW);

        GlElementBufferGuard ebo_guard(ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh_resource->index_buffer_.size() * sizeof(unsigned int), mesh_resource->index_buffer_.data(), GL_STATIC_DRAW);

        // position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void *)0);
        // normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void *)(offsetof(MeshVertex, normal)));
        // tex_coord
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void *)(offsetof(MeshVertex, tex_coord)));
        // tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void *)(offsetof(MeshVertex, tangent)));
        // bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void *)(offsetof(MeshVertex, bitangent)));

        return GeometryBuffer(vao, vbo, ebo);
    }
    RenderMeshResource *RenderMesh::GetMeshResource(unsigned int index)
    {
        return lod_mesh_resources_.at(index) ? lod_mesh_resources_[index].get() : nullptr;
    }

    void RenderMesh::SetMaterial(const std::shared_ptr<RenderMaterial> &material, unsigned int section_index)
    {

        bool is_index_valid = section_index >= 0 && section_index < mesh_resource_->mesh_sections_.size();
        if (!is_index_valid)
        {
            KP_LOG("MeshLog", LOG_LEVEL_ERROR, "try to access(set material) invalid mesh section with index: %d", section_index);
            return;
        }
        mesh_resource_->mesh_sections_.at(section_index).material = material;
    }

    std::shared_ptr<RenderMaterial> RenderMesh::GetMaterial(unsigned int section_index)
    {
        bool is_index_valid = section_index >= 0 && section_index < mesh_resource_->mesh_sections_.size();
        if (!is_index_valid)
        {
            KP_LOG("MeshLog", LOG_LEVEL_ERROR, "try to access(get material) invalid mesh section with index: %d", section_index);
            return nullptr;
        }

        return mesh_resource_->mesh_sections_.at(section_index).material;
    }

    unsigned int RenderMesh::CalculateLODCount(unsigned int triangle_count)
    {
        if (triangle_count < 500)
        {
            return 0;
        }
        else if (triangle_count < 5'000)
        {
            return 1;
        }
        else if (triangle_count < 20'000)
        {
            return 2;
        }
        else
        {
            return 3;
        }
    }

    void RenderMesh::UpdateLOD(const Vector3f &camera_pos, const Matrix4f &transform)
    {
        Vector3f world_aabb_center = GetWorldAABB(mesh_resource_->aabb_, transform).Center();
        float distance = (float)(camera_pos - world_aabb_center).Norm();
        // match LOD
        unsigned int new_lod = std::min(GetLODLevelFromDistance(distance), lod_max_level);
        if (new_lod == lod_level)
        {
            return;
        }
        lod_level = new_lod;
        mesh_resource_ = GetMeshResource(lod_level);
        geometry_buf_ = geometry_buffers_[lod_level];
    }

    unsigned int RenderMesh::GetLODLevelFromDistance(float distance)
    {
        if (distance < 20)
        {
            return 0;
        }
        else if (distance < 50)
        {
            return 1;
        }
        else if (distance < 100)
        {
            return 2;
        }
        else
        {
            return 3;
        }
    }

    AABB RenderMesh::GetAABB() const
    {
        return mesh_resource_ ? mesh_resource_->aabb_ : AABB();
    }

    void RenderMesh::BuildLODMeshResource(unsigned int level)
    {
        std::unique_ptr<RenderMeshResource> lod_resource = std::make_unique<RenderMeshResource>();

        for (const MeshSection &section : mesh_resource_->mesh_sections_)
        {
            unsigned int start = section.index_start;
            unsigned int count = section.index_count;

            std::vector<unsigned int> input_indices(
                mesh_resource_->index_buffer_.begin() + start,
                mesh_resource_->index_buffer_.begin() + start + count);

            std::vector<unsigned int> remap(count);
            size_t vertex_count = mesh_resource_->vertex_buffer_.size();

            size_t unique_vertex_count = meshopt_generateVertexRemap(
                remap.data(),
                input_indices.data(),
                count,
                mesh_resource_->vertex_buffer_.data(),
                vertex_count,
                sizeof(MeshVertex));

            std::vector<MeshVertex> remapped_vertices(unique_vertex_count);
            meshopt_remapVertexBuffer(
                remapped_vertices.data(),
                mesh_resource_->vertex_buffer_.data(),
                vertex_count,
                sizeof(MeshVertex),
                remap.data());

            std::vector<unsigned int> remapped_indices(count);
            meshopt_remapIndexBuffer(
                remapped_indices.data(),
                input_indices.data(),
                count,
                remap.data());

            const float simplify_ratio = 1.f - level * 0.2f;
            unsigned int target_index_count = static_cast<unsigned int>(simplify_ratio * count);
            const float error_threshold = 0.01f; // loosen up
            float result_error = 0.0f;

            std::vector<unsigned int> simplified_indices(count);
            size_t simplified_index_count = meshopt_simplify(
                simplified_indices.data(),
                remapped_indices.data(),
                remapped_indices.size(),
                reinterpret_cast<const float *>(&remapped_vertices[0].position),
                remapped_vertices.size(),
                sizeof(MeshVertex),
                target_index_count,
                error_threshold,
                0,
                &result_error);
            simplified_indices.resize(simplified_index_count);

            // std::cout << "[LOD] Simplified from " << remapped_indices.size()
            //           << " to " << simplified_index_count
            //           << " (vertices: " << remapped_vertices.size()
            //           << "), error: " << result_error << std::endl;

            // Append to output buffers
            unsigned int vertex_offset = static_cast<unsigned int>(lod_resource->vertex_buffer_.size());
            unsigned int index_offset = static_cast<unsigned int>(lod_resource->index_buffer_.size());

            // Offset indices
            for (unsigned int &index : simplified_indices)
                index += vertex_offset;

            lod_resource->vertex_buffer_.insert(
                lod_resource->vertex_buffer_.end(),
                remapped_vertices.begin(),
                remapped_vertices.end());

            lod_resource->index_buffer_.insert(
                lod_resource->index_buffer_.end(),
                simplified_indices.begin(),
                simplified_indices.end());

            lod_resource->mesh_sections_.push_back(MeshSection(index_offset,
                                                               simplified_indices.size(),
                                                               section.material)

            );
        }

        geometry_buffers_.push_back(CreateGeometryBuffer(lod_resource.get()));
        lod_mesh_resources_.push_back(std::move(lod_resource));
    }

    RenderMesh::~RenderMesh()
    {
        glDeleteVertexArrays(1, &geometry_buf_.vao);
        glDeleteBuffers(1, &geometry_buf_.vbo);
        glDeleteBuffers(1, &geometry_buf_.ebo);
    }

}