#ifndef KPENGINE_RUNTIME_RENDER_MESH_H
#define KPENGINE_RUNTIME_RENDER_MESH_H

#include <vector>
#include <string>
#include <memory>

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
    const RenderMeshResource* GetMeshResource() const;
    std::string GetName() const{return name_;}
    ~RenderMesh();
public:
    unsigned int vao_;
    unsigned int vbo_;
    unsigned int ebo_;
private:
    //std::vector<std::unique_ptr<RenderMeshResource>> lod_mesh_resources;
    std::unique_ptr<RenderMeshResource> mesh_resource;
    std::string name_;
    unsigned int lod_level;

};
}



#endif