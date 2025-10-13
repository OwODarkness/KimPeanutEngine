#include "opengl_mesh.h"
#include "opengl_context.h"
#include "opengl_backend.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    MeshResource OpenglMesh::GetMeshHandle() const
    {
        return {&resource_};
    }
    void OpenglMesh::Initialize(const GraphicsContext &context, const MeshData &data)
    {
        if(context.type != GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            KP_LOG("OpenglMeshLog", LOG_LEVEL_ERROR, "Invalid Graphics API for OpenglMesh");
            throw std::runtime_error("Invalid Graphics API for OpenglMesh");
        }

        OpenglContext* opengl_context = static_cast<OpenglContext*>(context.native);
        OpenglBackend* backend = opengl_context->backend;

        size_t vertex_size = data.vertices.size() * sizeof(Vertex);
        BufferHandle vertex_buffer_handle = backend->CreateVertexBuffer(data.vertices.data(), vertex_size);

        size_t index_size = data.indices.size() * sizeof(uint32_t);
        BufferHandle index_buffer_handle = backend->CreateIndexBuffer(data.indices.data(), index_size);

        resource_.vbo = vertex_buffer_handle.id;
        resource_.ebo = index_buffer_handle.id;
        resource_.sections = data.sections;
    }
    void OpenglMesh::Destroy(const GraphicsContext &context)
    {
    }
}