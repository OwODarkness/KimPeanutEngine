#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H

#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>

#include "common/render_backend.h"

namespace kpengine::graphics
{
    class VulkanBackend : public RenderBackend
    {
    public:
        virtual void Initialize() override;
        virtual void BeginFrame() override;
        virtual void EndFrame() override;
        virtual void Present() override;
        virtual void Cleanup() override;

    private:
        void CreateInstance();
        void CreateDebugMessager();
    private:
        std::vector<const char *> GetRequiredExtensions() const;
        bool CheckValidationLayerSupport(const std::vector<const char *> &validation_layers) const;
    private:
        VkInstance instance_;
        VkDebugUtilsMessengerEXT debug_messager_;

        std::vector<const char *> validation_layers = {
            "VK_LAYER_KHRONOS_validation"};
        std::vector<const char *> device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    };
}

#endif