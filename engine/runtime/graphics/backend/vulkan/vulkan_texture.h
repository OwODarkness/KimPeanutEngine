#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_TEXTURE_H

#include <vulkan/vulkan.h>
#include "vulkan_enum.h"
#include "common/texture.h"
#include "vulkan_memory_allocator.h"
namespace kpengine::graphics
{

    class VulkanBackend;

    struct VulkanTextureResource
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    inline VulkanTextureResource ConvertToVulkanTextureResource(const TextureResource &resource)
    {
        VulkanTextureResource res;
        res.image = reinterpret_cast<VkImage>(resource.image);
        res.view = reinterpret_cast<VkImageView>(resource.view);
        return res;
    }

    inline TextureResource ConvertToTextureResource(const VulkanTextureResource &resource)
    {
        TextureResource res;
        res.image = reinterpret_cast<TextureImage>(resource.image);
        res.view = reinterpret_cast<TextureView>(resource.view);
        return res;
    }


    class VulkanTexture : public Texture
    {
    public:
        ~VulkanTexture();
        TextureResource GetTextueHandle() const override{return ConvertToTextureResource(resource_);}
    protected:
        void Initialize(GraphicsContext context, const TextureData& data, const TextureSettings& settings) override;
        void Destroy(GraphicsContext context) override;
        

    private:
        VulkanTextureResource resource_;
        VulkanMemoryAllocation allocation_;
    };
}

#endif