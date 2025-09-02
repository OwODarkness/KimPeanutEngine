#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_TEXTURE_H

#include <vulkan/vulkan.h>
#include "vulkan_enum.h"
#include "common/texture.h"

namespace kpengine::graphics
{

    class VulkanBackend;

    struct VulkanTextureResource
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    inline VulkanTextureResource ConvertToVulkanTextureResource(const TextureResource &resource)
    {
        VulkanTextureResource res;
        res.image = reinterpret_cast<VkImage>(resource.image);
        res.view = reinterpret_cast<VkImageView>(resource.view);
        res.sampler = reinterpret_cast<VkSampler>(resource.sampler);
        return res;
    }

    inline TextureResource ConvertToTextureResource(const VulkanTextureResource &resource)
    {
        TextureResource res;
        res.image = reinterpret_cast<TextureImage>(resource.image);
        res.view = reinterpret_cast<TextureView>(resource.view);
        res.sampler = reinterpret_cast<TextureSampler>(resource.sampler);
        return res;
    }


    class VulkanTexture : public Texture
    {
    public:
        VulkanTexture(VkDevice device);
        void Initialize(const TextureDesc &desc) override;
        void Destroy() override;
        TextureResource GetTextueHandle() const override{return ConvertToTextureResource(resource_);}

    private:
        VkDevice device_;
        VulkanTextureResource resource_;
    };
}

#endif