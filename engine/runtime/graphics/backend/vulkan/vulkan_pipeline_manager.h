#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_PIPELINE_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_PIPELINE_MANAGER_H


#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "common/api.h"
#include "common/pipeline_desc.h"

namespace kpengine::graphics{

    struct VulkanPipelineResource{
        std::vector<VkDescriptorSetLayout> descriptor_set_layouts; 
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        uint32_t generation = 0;
    };

    class VulkanPipelineManager{
    public:
        PipelineHandle CreatePipelineResource(VkDevice logical_device, const PipelineDesc& pipeline_desc, VkRenderPass renderpass);
        void DestroyPipelineResource(VkDevice logical_device, PipelineHandle handle);
        VulkanPipelineResource* GetPipelineResource(PipelineHandle handle);
    private:
        void CreateShaderModule(VkDevice logiccal_device, const void* data, size_t, VkShaderModule& shader_module);
    private:
        std::vector<VulkanPipelineResource> pipelines_;
        std::vector<uint32_t> free_slots_;
    };
}

#endif