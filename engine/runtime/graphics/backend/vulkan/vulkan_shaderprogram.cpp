#include "vulkan_shaderprogram.h"


namespace kpengine::graphics{
        VulkanShaderProgram::VulkanShaderProgram(VkDevice device, const std::string& name, const std::string& vert_path, const std::string& frag_path, const std::string& geom_path):
    ShaderProgram(name_)
    {
    }
    void VulkanShaderProgram::Bind() 
    {

    }
    void VulkanShaderProgram::Unbind() 
    {

    }
    void VulkanShaderProgram::Cleanup() 
    {

    }

    std::vector<uint32_t> VulkanShaderProgram::ReadShaderFile(const std::string& path) const
    {
        
    }
}