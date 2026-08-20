#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_DESCRIPTOR_SET_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_DESCRIPTOR_SET_MANAGER_H

#include <vector>

#include <vulkan/vulkan.h>

#include "common/resource_binding.h"

namespace kpengine::graphics
{
    class SamplerManager;
    class TextureManager;
    class VulkanBufferManager;
    struct VulkanPipelineResource;

    struct VulkanDescriptorSetResource
    {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };

    class VulkanDescriptorSetManager
    {
    public:
        DescriptorSetHandle CreateResourceBindingSet(
            VkDevice logical_device, const VulkanPipelineResource &pipeline, const ResourceBindingSetDesc &desc,
            VulkanBufferManager &buffers, TextureManager &textures, SamplerManager &samplers);
        bool DestroyResourceBindingSet(VkDevice logical_device, DescriptorSetHandle handle);
        void DestroyAll(VkDevice logical_device);
        VkDescriptorSet GetDescriptorSet(DescriptorSetHandle handle);

    private:
        std::vector<VulkanDescriptorSetResource> resources_;
        HandleSystem<DescriptorSetHandle> handle_system_;
    };
}

#endif
