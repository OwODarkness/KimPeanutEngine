#include "vulkan_shader_module.h"
#include "log/logger.h"
#include "vulkan/vulkan_context.h"

namespace kpengine::graphics
{
    void VulkanShaderModule::Initialize(const GraphicsContext &context, std::shared_ptr<ShaderData> shader)
    {
        if (shader == nullptr)
        {
            throw std::runtime_error("hader data is nullptr");
        }
        if (context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            return;
        }
        VulkanContext *context_ptr = static_cast<VulkanContext *>(context.native);
        device_ = context_ptr->logical_device;
        stage_ = shader->stage;
        entry_point_ = shader->entry;

        VkShaderModuleCreateInfo shader_module_create_info{};
        shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_module_create_info.codeSize = shader->spirv.size();
        shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(shader->spirv.data());

        if (vkCreateShaderModule(device_, &shader_module_create_info, nullptr, &handle_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shader module");
        }
    }
    void VulkanShaderModule::Destroy()
    {
        if (handle_)
            vkDestroyShaderModule(device_, handle_, nullptr);
    }
    const void *VulkanShaderModule::GetHandle() const
    {
        return &handle_;
    }

    ShaderStage VulkanShaderModule::GetStage() const
    {
        return stage_;
    }
    const std::string &VulkanShaderModule::GetEntryPoint() const
    {
        return entry_point_;
    }
}