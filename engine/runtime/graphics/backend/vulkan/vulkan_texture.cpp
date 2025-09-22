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

        BufferHandle stage_handle;
        if (!data.pixels.empty())
        {
            stage_handle = backend->CreateStageBufferResource(image_size);
            backend->UploadDataToBuffer(stage_handle, image_size, data.pixels.data());
        }

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

        allocation_ = backend->GetImageMemoryPool()->AllocateImageMemory(physical_device, logical_device, resource_.image);

        if ((settings.usage & TextureUsage::TEXTURE_USAGE_SAMPLE) == TextureUsage::TEXTURE_USAGE_SAMPLE)
        {
            backend->TransitionImageLayout(resource_.image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_TRANSFER_DST);
            backend->ReleaseImageOwnerShip(resource_.image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_TRANSFER_DST);
            backend->AcquireImageOwnerShip(resource_.image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_TRANSFER_DST);
            backend->CopyBufferToImage(stage_handle, resource_.image, data.width, data.height);
            backend->ReleaseImageOwnerShip(resource_.image, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, TextureUsage::TEXTURE_USAGE_SAMPLE);
            backend->AcquireImageOwnerShip(resource_.image, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, TextureUsage::TEXTURE_USAGE_SAMPLE);
            backend->TransitionImageLayout(resource_.image, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, TextureUsage::TEXTURE_USAGE_SAMPLE);
            backend->DestroyBufferResource(stage_handle);
        }
        else if((settings.usage & TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT) == TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT)
        {
            backend->TransitionImageLayout(resource_.image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT);
        }


        VkImageViewCreateInfo view_create_info{};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = resource_.image;
        view_create_info.format = ConvertToVulkanTextureFormat(settings.format);
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.subresourceRange.aspectMask = ConvertToVulkanImageAspect(settings.aspect);
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(logical_device, &view_create_info, nullptr, &resource_.view) != VK_SUCCESS)
        {
            KP_LOG("VulkanTextureLog", LOG_LEVEL_ERROR, "Failed to create image view");
            throw std::runtime_error("Failed to create image view");
        }

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

        vkDestroyImageView(logical_device, resource_.view, nullptr);
        vkDestroyImage(logical_device, resource_.image, nullptr);
        backend->GetImageMemoryPool()->Free(logical_device, allocation_);
    }

    VulkanTexture::~VulkanTexture() = default;

}