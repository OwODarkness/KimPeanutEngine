#ifndef KPENGINE_RUNTIME_RENDER_MESH_H
#define KPENGINE_RUNTIME_RENDER_MESH_H

#include <vector>
#include <string>
#include <memory>
#include "math/math_header.h"
#include "aabb.h"
#include "geometry_buffer.h"

namespace kpengine{
class RenderMaterial;
class RenderShader;
class RenderMeshResource;

class RenderMesh
{
public:
    RenderMesh();
    RenderMesh(const std::string& mesh_relative_path);
    
    void Initialize();
    GeometryBuffer CreateGeometryBuffer(RenderMeshResource* mesh_resource);
    RenderMeshResource* GetMeshResource(size_t  index = 0);
    void SetMaterial(const std::shared_ptr<RenderMaterial>& material, size_t  section_index);
    std::shared_ptr<RenderMaterial> GetMaterial(size_t  index);
    void UpdateLOD(const Vector3f& camera_pos, const Matrix4f& transform);
    size_t CalculateLODCount(size_t triangle_count);
    void BuildLODMeshResource(size_t level);
    AABB GetAABB() const;
    std::string GetName() const{return name_;}
    ~RenderMesh();
protected:
    size_t  GetLODLevelFromDistance(float distance);
public:
    //geometry_buffer and mesh_resource in runtime
    GeometryBuffer geometry_buf_;
    RenderMeshResource* mesh_resource_;
private:
    //mesh_resource and geometry_buffer for each LOD
    std::vector<std::unique_ptr<RenderMeshResource>> lod_mesh_resources_;
    std::vector<GeometryBuffer> geometry_buffers_;
    std::string name_;
    size_t  lod_level;
    size_t  lod_max_level;
    bool is_initialized_ = false;
};
}



#endif