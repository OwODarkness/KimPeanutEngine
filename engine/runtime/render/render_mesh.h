#ifndef KPENGINE_RUNTIME_RENDER_MESH_H
#define KPENGINE_RUNTIME_RENDER_MESH_H

#include <vector>
#include <string>
#include <memory>
#include "runtime/core/math/math_header.h"
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
    RenderMeshResource* GetMeshResource(unsigned int index = 0);
    void SetMaterial(const std::shared_ptr<RenderMaterial>& material, unsigned int section_index);
    std::shared_ptr<RenderMaterial> GetMaterial(unsigned int index);
    void UpdateLOD(const Vector3f& camera_pos, const Matrix4f& transform);
    unsigned int CalculateLODCount(unsigned int triangle_count);
    void BuildLODMeshResource(unsigned int level);

    std::string GetName() const{return name_;}
    ~RenderMesh();
protected:
    unsigned int GetLODLevelFromDistance(float distance);
public:
    //geometry_buffer and mesh_resource in runtime
    GeometryBuffer geometry_buf_;
    RenderMeshResource* mesh_resource_;
private:
    //mesh_resource and geometry_buffer for each LOD
    std::vector<std::unique_ptr<RenderMeshResource>> lod_mesh_resources_;
    std::vector<GeometryBuffer> geometry_buffers_;
    std::string name_;
    unsigned int lod_level;
    unsigned int lod_max_level;
    bool is_initialized_ = false;
};
}



#endif