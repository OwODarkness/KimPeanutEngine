#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_SAHDER_PROGRAM_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_SHADER_PROGRAM_H

#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "common/shaderprogram.h"

namespace kpengine::graphics{
class VulkanShaderProgram: public ShaderProgram{
public:
    VulkanShaderProgram(VkDevice device, const std::string& name, const std::string& vert_path, const std::string& frag_path, const std::string& geom_path = "");
    void Bind() override;
    void Unbind() override;
    void Cleanup() override;
private:
    VkDevice device;
    VkShaderModule vert_shader_module_;
    VkShaderModule frag_shader_module_;
    VkShaderModule geom_shader_module_;
};
}

#endif