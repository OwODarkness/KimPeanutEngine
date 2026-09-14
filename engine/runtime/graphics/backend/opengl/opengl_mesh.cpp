#include "opengl_mesh.h"
#include "opengl_context.h"
#include "opengl_backend.h"
#include "log/logger.h"

#include <span>
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

        BufferHandle vertex_buffer_handle = backend->CreateVertexBuffer(
            std::as_bytes(std::span{data.vertices}));

        BufferHandle index_buffer_handle = backend->CreateIndexBuffer(
            std::as_bytes(std::span{data.indices}));

        resource_.vbo = vertex_buffer_handle.id;
        resource_.ebo = index_buffer_handle.id;
        resource_.sections = data.sections;
    }
    void OpenglMesh::Destroy(const GraphicsContext &context)
    {
        if (resource_.vbo)
        {
            glDeleteBuffers(1, &resource_.vbo);
            resource_.vbo = 0;
        }
        if (resource_.ebo)
        {
            glDeleteBuffers(1, &resource_.ebo);
            resource_.ebo = 0;
        }
    }
}
