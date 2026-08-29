#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_PIPELINE_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_PIPELINE_MANAGER_H


#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "common/api.h"
#include "common/pipeline_types.h"
namespace kpengine::graphics{

    struct VulkanDescriptorSetLayout{
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bool owned = true;
    };

    struct VulkanPipelineResource{
        std::vector<VulkanDescriptorSetLayout> descriptor_set_layouts;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        // Baked attachment formats so a recorder can validate target
        // compatibility before recording draws.
        std::vector<TextureFormat> color_attachment_formats;
        TextureFormat depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        uint32_t rasterization_samples = 1;
    };

    class VulkanPipelineManager{
    public:
        PipelineHandle CreatePipelineResource(VkDevice logical_device, const PipelineDesc& pipeline_desc,
                                              VkDescriptorSetLayout bindless_layout = VK_NULL_HANDLE);
        bool DestroyPipelineResource(VkDevice logical_device, PipelineHandle handle);
        void DestroyAll(VkDevice logical_device);
        VulkanPipelineResource* GetPipelineResource(PipelineHandle handle);
    private:
        void CreateShaderModule(VkDevice logiccal_device, const void* data, size_t, VkShaderModule& shader_module);
    private:
        std::vector<VulkanPipelineResource> resources_;
        HandleSystem<PipelineHandle> handle_system_;
    };
}

#endif
