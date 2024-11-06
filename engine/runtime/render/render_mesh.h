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
    void Initialize(std::shared_ptr<RenderShader> shader_helper);
    void Draw();
    virtual ~RenderMesh();
private:
    std::vector<Vertex> verticles_;
    std::vector<unsigned int> indices_; 
    std::shared_ptr<RenderMaterial> material_;
    std::shared_ptr<RenderShader> shader_helper_;
    unsigned int vbo_;
    unsigned int vao_;
    unsigned int ebo_;
};  
}



#endif