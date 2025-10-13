#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_MESH_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_MESH_H

#include "common/mesh.h"
#include "glad/glad.h"
namespace kpengine::graphics{
    struct OpenglMeshResource{
        GLuint vbo;
        GLuint ebo;
        std::vector<MeshSection> sections;
    };
    class OpenglMesh: public Mesh{
    public:
        MeshResource GetMeshHandle() const override;
        void Initialize(const GraphicsContext& context, const MeshData& data) override;
        void Destroy(const GraphicsContext& context) override;
    private:
        OpenglMeshResource resource_;
    };
}


#endif