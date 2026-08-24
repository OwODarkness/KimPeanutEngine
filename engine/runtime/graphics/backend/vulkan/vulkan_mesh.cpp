#include "vulkan_mesh.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_context.h"
#include "vulkan_upload_context.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    namespace
    {
        BufferHandle CreateDeviceBuffer(VulkanContext &context, const void *data,
                                        VkDeviceSize size, VkBufferUsageFlags usage)
        {
            if (!context.buffer_manager || !context.upload_context || !data || size == 0)
            {
                throw std::runtime_error("Vulkan mesh resource services are unavailable");
            }

            VkBufferCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            create_info.size = size;
            create_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            const BufferHandle handle = context.buffer_manager->CreateBufferResource(
                context.logical_device, &create_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);
            try
            {
                context.upload_context->UploadBuffer(handle, static_cast<size_t>(size), data);
            }
            catch (...)
            {
                context.buffer_manager->DestroyBufferResource(context.logical_device, handle);
                throw;
            }
            return handle;
        }

        void DestroyDeviceBuffer(VulkanContext &context, BufferHandle handle)
        {
            if (handle.IsValid() && context.buffer_manager)
            {
                context.buffer_manager->DestroyBufferResource(context.logical_device, handle);
            }
        }
    }

    void VulkanMesh::Initialize(const GraphicsContext &context, const MeshData &data)
    {
        if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG("VulkanMeshLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanMesh");
            throw std::runtime_error("Invalid Graphics API for VulkanMesh");
        }

        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        if (!context_ptr)
        {
            throw std::runtime_error("Vulkan mesh context is unavailable");
        }

        const VkDeviceSize vertices_size = sizeof(Vertex) * data.vertices.size();
        resource_.vertex_handle = CreateDeviceBuffer(*context_ptr, data.vertices.data(), vertices_size,
                                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        const VkDeviceSize indices_size = sizeof(uint32_t) * data.indices.size();
        resource_.index_handle = CreateDeviceBuffer(*context_ptr, data.indices.data(), indices_size,
                                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        resource_.sections = data.sections;
    }
    void VulkanMesh::Destroy(const GraphicsContext &context)
    {
        if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG("VulkanMeshLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanMesh");
            throw std::runtime_error("Invalid Graphics API for VulkanMesh");
        }

        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        if (!context_ptr)
        {
            return;
        }

        DestroyDeviceBuffer(*context_ptr, resource_.vertex_handle);
        DestroyDeviceBuffer(*context_ptr, resource_.index_handle);
    }

    MeshResource VulkanMesh::GetMeshHandle() const
    {
        return {&resource_};
    }
}
