#ifndef KPENGINE_GRAPHICS_VULKAN_SHADER_MODULE_H
#define KPENGINE_GRAPHICS_VULKAN_SHADER_MODULE_H

#include "common/shader_module.h"
#include <vulkan/vulkan.h>

namespace kpengine::graphics{
    class VulkanShaderModule: public ShaderModule
    {
    public:
        void Initialize(const GraphicsContext& context, std::shared_ptr<ShaderData> shader) override;
        void Destroy() override;
        const void* GetHandle() const;
        ShaderStage GetStage() const;
        const std::string& GetEntryPoint() const;
    private:
         VkShaderModule handle_ = VK_NULL_HANDLE;
         VkDevice device_;
         ShaderStage stage_;
         std::string entry_point_;
    };
}

#endif