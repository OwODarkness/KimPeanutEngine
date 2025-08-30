#include "vulkan_shader.h"
#include "tool/shaderloader.h"
#include "log/logger.h"

namespace kpengine::graphics
{
    VulkanShader::VulkanShader(VkDevice device, const std::string &name, ShaderType type, const std::string &path) : Shader(name, type), device_(device)
    {
        CreateShaderModule(path, shader_module_);
    }

    VkPipelineShaderStageCreateInfo VulkanShader::GetPipelineShaderStageCreateInfo() const
    {
        VkPipelineShaderStageCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        create_info.module = shader_module_;
        create_info.pName = "main";

        if (type_ == ShaderType::SHADER_TYPE_VERTEX)
        {
            create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        }
        else if (type_ == ShaderType::SHADER_TYPE_FRAGMENT)
        {
            create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        else if (type_ == ShaderType::SHADER_TYPE_GEOMETRY)
        {
            create_info.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        }
        return create_info;
    }
    void VulkanShader::CreateShaderModule(const std::string &path, VkShaderModule &module)
    {
        std::vector<char> shader_code = ShaderLoader::ReadBinaryFile(path);

        VkShaderModuleCreateInfo shader_module_create_info{};
        shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_module_create_info.codeSize = static_cast<uint32_t>(shader_code.size());
        shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(shader_code.data());

        if (vkCreateShaderModule(device_, &shader_module_create_info, nullptr, &module) != VK_SUCCESS)
        {
            KP_LOG("VulkanShaderLog", LOG_LEVEL_ERROR, "Failed to create shader module: " + path);
            throw std::runtime_error("Failed to create shader module");
        }


    }

    VulkanShader::~VulkanShader()
    {
        vkDestroyShaderModule(device_, shader_module_, nullptr);
    }
}