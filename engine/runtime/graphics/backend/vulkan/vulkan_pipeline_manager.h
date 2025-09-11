#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_PIPELINE_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_PIPELINE_MANAGER_H


#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "common/api.h"
#include "common/pipeline_desc.h"

namespace kpengine::graphics{

    struct VulkanPipelineResource{

        VkPipelineLayout layout;
        VkPipeline pipeline;
    };

    class VulkanPipelineManager{
    public:
        PipelineHandle CreatePipeline(VkDevice logicial_device, const PipelineDesc& pipeline_desc);
    private:
        void CreateShaderModule(VkDevice logicial_device, const void* data, size_t, VkShaderModule& shader_module);
    private:
        std::vector<VulkanPipelineResource> pipelines_;
        std::vector<uint32_t> free_slots_;
    };
}

#endif