#include "vulkan_texture.h"
#include "vulkan_backend.h"
#include "vulkan_context.h"
#include "log/logger.h"
#include "vulkan_backend.h"
#include "vulkan_image_memory_pool.h"

namespace kpengine::graphics
{

    void VulkanTexture::Initialize(GraphicsContext device, const TextureData& data, const TextureSettings& settings)
    {
        if(device.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
           KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Invalid Graphics API for VulkanTexture");
           throw std::runtime_error("Invalid Graphics API for VulkanTexture"); 
        } 
        
        VulkanContext* device_ptr = static_cast<VulkanContext*>(device.native);
        VkPhysicalDevice physical_device = device_ptr->physical_device;
        VkDevice logical_device = device_ptr->logical_device;
        VulkanBackend* backend = device_ptr->backend;


        size_t image_size = data.pixels.size();

        BufferHandle stage_handle = backend->CreateStageBufferResource(image_size);
        backend->UploadDataToBuffer(stage_handle,  image_size, data.pixels.data());
        

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

        if(vkCreateImage(logical_device, &image_create_info, nullptr, &resource_.image) != VK_SUCCESS)
        {
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Failed to create Image");
            throw std::runtime_error("Failed to create image");
        }
        
        backend->GetImageMemoryPool()->AllocateImageMemory(physical_device, logical_device, resource_.image );

        // VkImageViewCreateInfo view_create_info{};

        // VkSamplerCreateInfo sampler_create_info{};


        
    }
    void VulkanTexture::Destroy(GraphicsContext device)
    {
        // vkDestroyImage(device_, resource_.image, nullptr);
        // vkDestroyImageView(device_, resource_.view, nullptr);
        // vkDestroySampler(device_, resource_.sampler, nullptr);
    }

}