#ifndef KPENGINE_RUNTIME_RENDER_MESH_H
#define KPENGINE_RUNTIME_RENDER_MESH_H

#include <vector>
#include <memory>

#include <glm/glm.hpp>

namespace kpengine{
class RenderMaterial;
class RenderShader;
struct Vertex{
    glm::vec3 location;
    glm::vec3 normal;
    glm::vec2 tex_coord;
};

class RenderMesh{
public:
    RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned int> indices, std::shared_ptr<RenderMaterial> material);
    virtual void Initialize(std::shared_ptr<RenderShader> shader_helper);
    virtual void Draw();
    virtual ~RenderMesh();
protected:
    std::vector<Vertex> verticles_;
    std::shared_ptr<RenderMaterial> material_;
    std::shared_ptr<RenderShader> shader_helper_;
public:
    std::vector<unsigned int> indices_; 
    unsigned int vbo_;
    unsigned int vao_;
};  

class RenderMeshStandard : public RenderMesh{
public:
    RenderMeshStandard(std::vector<Vertex> verticles, std::vector<unsigned int> indices, std::shared_ptr<RenderMaterial> material);
    virtual void Initialize(std::shared_ptr<RenderShader> shader_helper) override;
    virtual void Draw() override;
    virtual ~RenderMeshStandard();
protected:
    unsigned int ebo_;

};

class SkyBox : public RenderMesh{
public:
    SkyBox(std::shared_ptr<RenderMaterial> material);
    virtual void Initialize(std::shared_ptr<RenderShader> shader_helper) override;
    virtual void Draw() override;

};
}



#endif