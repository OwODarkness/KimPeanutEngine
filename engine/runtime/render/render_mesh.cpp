#include "render_mesh.h"
#include <glad/glad.h>
namespace kpengine{
   RenderMesh::RenderMesh(std::vector<Vertex> verticles, std::vector<unsigned> indices):
    verticles_(verticles),indices_(indices)
    {
    }

    RenderMesh::~RenderMesh()
    {
        
    }

    void RenderMesh::Initialize()
    {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, verticles_.size() * sizeof(Vertex) , &verticles_[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(2); 
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),  (void*)(offsetof(Vertex, tex_coord)));

        glBindVertexArray(0);
    }

    void RenderMesh::Draw()
    {
        glBindVertexArray(vao_);
        glDrawElements(GL_TRIANGLES, indices_.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
}