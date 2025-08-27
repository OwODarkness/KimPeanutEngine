#include "vulkan_texture.h"
#include "vulkan_backend.h"
namespace kpengine::graphics
{
    VulkanTexture::VulkanTexture(VulkanBackend* backend) : Texture(), backend_(backend){}

    void VulkanTexture::Initialize(const TextureDesc &desc)
    {
        VkImageCreateInfo image_create_info{};
        VkImageViewCreateInfo view_create_info{};
        VkSamplerCreateInfo sampler_create_info{};
    }
    void VulkanTexture::Destroy()
    {
        VkDevice device = backend_->GetLogicialDevice();
        vkDestroyImage(device, handle_.image, nullptr);
        vkDestroyImageView(device, handle_.view, nullptr);
        vkDestroySampler(device, handle_.sampler, nullptr);
    }

}