#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_TEXTURE_H

#include <vulkan/vulkan.h>
#include "vulkan_enum.h"
#include "common/texture.h"

namespace kpengine::graphics
{

    class VulkanBackend;

    struct VulkanTextureHandle
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    inline VulkanTextureHandle ConvertToVulkanTextureHandle(const TextureHandle &handle)
    {
        VulkanTextureHandle res;
        res.image = reinterpret_cast<VkImage>(handle.image);
        res.view = reinterpret_cast<VkImageView>(handle.view);
        res.sampler = reinterpret_cast<VkSampler>(handle.sampler);
        return res;
    }

    inline TextureHandle ConvertToTextureHandle(const VulkanTextureHandle &handle)
    {
        TextureHandle res;
        res.image = reinterpret_cast<TextureImage>(handle.image);
        res.view = reinterpret_cast<TextureView>(handle.view);
        res.sampler = reinterpret_cast<TextureSampler>(handle.sampler);
        return res;
    }


    class VulkanTexture : public Texture
    {
    public:
        VulkanTexture(VulkanBackend* backend);
        void Initialize(const TextureDesc &desc) override;
        void Destroy() override;
        TextureHandle GetTextueHandle() const override{return ConvertToTextureHandle(handle_);}

    private:
        VulkanBackend* backend_;
        VulkanTextureHandle handle_;
    };
}

#endif