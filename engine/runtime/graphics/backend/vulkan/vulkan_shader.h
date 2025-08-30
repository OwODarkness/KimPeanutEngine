#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_SAHDER_PROGRAM_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_SHADER_PROGRAM_H

#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "common/shader.h"

namespace kpengine::graphics{
class VulkanShader: public Shader{
public:
    VulkanShader(VkDevice device, const std::string& name, ShaderType type, const std::string& path);
    ~VulkanShader();
    VkPipelineShaderStageCreateInfo GetPipelineShaderStageCreateInfo() const;
private:
    void CreateShaderModule(const std::string& path, VkShaderModule& module);
private:
    VkDevice device_;
    VkShaderModule  shader_module_;
};
}

#endif