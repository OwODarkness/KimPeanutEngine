#ifndef KPENGINE_RUNTIME_RENDER_MESH_H
#define KPENGINE_RUNTIME_RENDER_MESH_H

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

#include "runtime/render/render_mesh_resource.h"
namespace kpengine{
class RenderMaterial;
class RenderShader;
class RenderMeshResource;


struct Vertex{
    glm::vec3 location;
    glm::vec3 normal;
    glm::vec2 tex_coord;
    glm::vec3 tangent;
};

class RenderMesh{
public:
    RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned int> indices, std::shared_ptr<RenderMaterial> material);
    virtual void Initialize();
    virtual void Draw(std::shared_ptr<RenderShader> shader_helper);
    virtual ~RenderMesh();

    size_t GetVerticlesNumber() const{return verticles_.size();}
    size_t GetTrianglesNumber() const{return indices_.size()/3;} 
protected:
    std::vector<Vertex> verticles_;
    std::shared_ptr<RenderMaterial> material_;
    std::shared_ptr<RenderShader> shader_helper_;
    int triangle_number_;
public:
    std::vector<unsigned int> indices_; 
    unsigned int vbo_;
    unsigned int vao_;
};  

class RenderMeshStandard : public RenderMesh{
public:
    RenderMeshStandard(std::vector<Vertex> verticles, std::vector<unsigned int> indices, std::shared_ptr<RenderMaterial> material);
    virtual void Initialize() override;
    virtual void Draw(std::shared_ptr<RenderShader> shader_helper) override;
    virtual ~RenderMeshStandard();
protected:
    unsigned int ebo_;

};



class RenderMesh_V2
{
public:
    RenderMesh_V2(const std::string& mesh_relative_path);
    void Initialize();
private:
    std::vector<RenderMeshResource> lod_mesh_resources;
    std::string name_;

};
}



#endif