#include "vulkan_texture.h"
#include "vulkan_backend.h"
#include "vulkan_context.h"
#include "log/logger.h"
#include "vulkan_backend.h"
#include "vulkan_image_memory_pool.h"
namespace kpengine::graphics
{

    void VulkanTexture::Initialize(GraphicsContext context, const TextureData &data, const TextureSettings &settings)
    {
        if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanTexture");
            throw std::runtime_error("Invalid Graphics API for VulkanTexture");
        }

        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        VkPhysicalDevice physical_device = context_ptr->physical_device;
        VkDevice logical_device = context_ptr->logical_device;
        VulkanBackend *backend = context_ptr->backend;

        size_t image_size = data.pixels.size();

        BufferHandle stage_handle = backend->CreateStageBufferResource(image_size);
        backend->UploadDataToBuffer(stage_handle, image_size, data.pixels.data());

        VkImageCreateInfo image_create_info{};
        image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.format = ConvertToVulkanTextureFormat(settings.format);
        image_create_info.imageType = ConvertToVulkanTextureType(settings.type);
        image_create_info.usage = ConvertToVulkanTextureUsage(settings.usage);
        image_create_info.mipLevels = settings.mip_levels;
        image_create_info.extent.width = data.width;
        image_create_info.extent.height = data.height;
        image_create_info.extent.depth = 1;
        image_create_info.arrayLayers = 1;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.samples = ConvertToVulkanSampleCount(settings.sample_count);
        image_create_info.flags = 0;

        if (vkCreateImage(logical_device, &image_create_info, nullptr, &resource_.image) != VK_SUCCESS)
        {
            backend->DestroyBufferResource(stage_handle);
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Failed to create Image");
            throw std::runtime_error("Failed to create image");
        }

        backend->GetImageMemoryPool()->AllocateImageMemory(physical_device, logical_device, resource_.image);

        backend->TransitionImageLayout(resource_.image, ConvertToVulkanTextureFormat(settings.format), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        backend->CopyBufferToImage(stage_handle, resource_.image, data.width, data.height);

        //backend->TransitionImageLayout(resource_.image, ConvertToVulkanTextureFormat(settings.format), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        backend->DestroyBufferResource(stage_handle);

        // VkImageViewCreateInfo view_create_info{};

        // VkSamplerCreateInfo sampler_create_info{};
    }
    void VulkanTexture::Destroy(GraphicsContext context)
    {
        if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanTexture");
            throw std::runtime_error("Invalid Graphics API for VulkanTexture");
        }

        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        VkPhysicalDevice physical_device = context_ptr->physical_device;
        VkDevice logical_device = context_ptr->logical_device;
        VulkanBackend *backend = context_ptr->backend;

        vkDestroyImage(logical_device, resource_.image, nullptr);
        backend->GetImageMemoryPool()->Destroy(logical_device);
    }

    VulkanTexture::~VulkanTexture() = default;

}