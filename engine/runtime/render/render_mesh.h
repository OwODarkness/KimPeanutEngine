#ifndef KPENGINE_RUNTIME_RENDER_MESH_H
#define KPENGINE_RUNTIME_RENDER_MESH_H

#include <vector>

#include <glm/glm.hpp>

namespace kpengine{

struct Vertex{
    glm::vec3 location;
    glm::vec3 normal;
    glm::vec2 tex_coord;
};

class RenderMesh{
public:
    RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned> indices);
    void Initialize();
    void Draw();
    virtual ~RenderMesh();
private:
    std::vector<Vertex> verticles_;
    std::vector<unsigned> indices_; 

    unsigned int vbo_;
    unsigned int vao_;
    unsigned int ebo_;
};  
}



#endif