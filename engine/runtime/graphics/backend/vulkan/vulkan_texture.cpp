#include "vulkan_texture.h"
#include "vulkan_backend.h"
namespace kpengine::graphics
{
    VulkanTexture::VulkanTexture(VkDevice device) : Texture(), device_(device){}

    void VulkanTexture::Initialize(GraphicsDevice device)
    {
        // VkImageCreateInfo image_create_info{};
        // image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        // image_create_info.format = ConvertToVulkanTextureFormat(desc.format);
        // image_create_info.imageType = ConvertToVulkanTextureType(desc.type);
        // image_create_info.usage = ConvertToVulkanTextureUsage(desc.usage);
        // image_create_info.mipLevels = desc.mip_levels;
        // image_create_info.extent.width = desc.width;
        // image_create_info.extent.height = desc.height;
        // image_create_info.extent.depth = desc.depth;
        
        // VkImageViewCreateInfo view_create_info{};

        // VkSamplerCreateInfo sampler_create_info{};

        
    }
    void VulkanTexture::Destroy(GraphicsDevice device)
    {
        vkDestroyImage(device_, resource_.image, nullptr);
        vkDestroyImageView(device_, resource_.view, nullptr);
        vkDestroySampler(device_, resource_.sampler, nullptr);
    }

}