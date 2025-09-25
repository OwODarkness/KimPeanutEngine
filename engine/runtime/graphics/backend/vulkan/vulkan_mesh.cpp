#include "vulkan_mesh.h"
#include "vulkan_backend.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    void VulkanMesh::Initialize(const GraphicsContext &context, const MeshData &data)
    {
        if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanMesh");
            throw std::runtime_error("Invalid Graphics API for VulkanMesh");
        }

        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        VulkanBackend* backend = context_ptr->backend;

        const VkDeviceSize vertices_size = sizeof(Vertex) * data.vertices.size();
        resource_.vertex_handle = backend->CreateVertexBuffer(data.vertices.data(), vertices_size);

        const VkDeviceSize indices_size = sizeof(uint32_t) * data.indices.size();
        resource_.index_handle = backend->CreateIndexBuffer(data.indices.data(), indices_size);
    
        resource_.sections = data.sections;
    }
    void VulkanMesh::Destroy(const GraphicsContext &context)
    {
                if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanMesh");
            throw std::runtime_error("Invalid Graphics API for VulkanMesh");
        }

        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        VulkanBackend* backend = context_ptr->backend;

        backend->DestroyBufferResource(resource_.vertex_handle);
        backend->DestroyBufferResource(resource_.index_handle);
    }

    MeshResource VulkanMesh::GetMeshHandle() const
    {
        return {&resource_};   
    }
}