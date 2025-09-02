#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H

#include <vector>
#include <cstdint>
#include <optional>
#include <vulkan/vulkan.h>

#include "common/render_backend.h"
#include "vulkan_buffer_pool.h"

namespace kpengine::graphics
{
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphics_family;
        std::optional<uint32_t> present_family;
        bool IsComplete() const
        {
            return graphics_family.has_value() && present_family.has_value();
        }

        static QueueFamilyIndices FindQueueFamilyIndices(VkPhysicalDevice physical_device, VkSurfaceKHR surface);
    };

    struct SwapchainSupportDetail
    {
        VkSurfaceCapabilitiesKHR capacities;
        std::vector<VkSurfaceFormatKHR> surface_formats;
        std::vector<VkPresentModeKHR> present_modes;

        static SwapchainSupportDetail FindSwapchainSupports(VkPhysicalDevice device, VkSurfaceKHR surface);
    };

    class VulkanBackend : public RenderBackend
    {
    public:
        virtual void Initialize() override;
        virtual void BeginFrame() override;
        virtual void EndFrame() override;
        virtual void Present() override;
        virtual void Cleanup() override;


    protected:
        BufferHandle CreateVertexBuffer(const void* data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void* data, size_t size) override;
        void DestroyBuffer(BufferHandle handle) override;

    public:
        VkDevice GetLogicialDevice() const { return logical_device_; }

    private:
        void CreateInstance();
        void CreateDebugMessager();
        void CreateSurface();
        void CreatePhysicalDevice();
        void CreateLogicalDevice();
        void CreateSwapchain();
        void CreateSwapchainImageViews();
        void CreateSwapchainRenderPass();
        void CreateGraphicsPipeline();
        void CreateFrameBuffers();
        void CreateCommandPool();
        void CreateCommandBuffer();
        void CreateSyncObjects();
        void CreateVertexBuffers();

        void CreateShaderModule(const void* data, size_t size, VkShaderModule &shader_module);

    private:
        void RecordCommandBuffer(VkCommandBuffer commandbuffer, uint32_t image_index);

    private:
        std::vector<const char *> FindRequiredExtensions() const;
        bool CheckValidationLayerSupport(const std::vector<const char *> &validation_layers) const;
        bool CheckDeviceExtensionsSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions) const;
        bool CheckPhysicalDeviceSuitable(VkPhysicalDevice device) const;

        VkSurfaceFormatKHR ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) const;
        VkPresentModeKHR ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR> &available_modes) const;
        VkExtent2D ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR &capacity) const;

    private:
        VkInstance instance_;
        VkDebugUtilsMessengerEXT debug_messager_;
        VkSurfaceKHR surface_;
        VkPhysicalDevice physical_device_;
        VkDevice logical_device_;
        VkQueue graphics_queue_;
        VkQueue present_queue_;
        VkSwapchainKHR swapchain_;
        VkExtent2D resolution_;
        VkFormat swapchain_image_format_;
        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_imageviews_;
        VkRenderPass swapchain_renderpass_;
        VkPipelineLayout pipeline_layout_;
        VkPipeline pipeline_;
        std::vector<VkFramebuffer> swapchain_framebuffers_;
        VkCommandPool command_pool_;
        VkCommandBuffer command_buffer_;
        VkSemaphore available_image_sepmaphore_;
        std::vector<VkSemaphore> render_finished_semaphores_;
        VkFence in_flight_fence_;

        VulkanBufferPool buffer_pool_;
        BufferHandle pos_handle_;
        BufferHandle color_handle_;
        BufferHandle index_handle_;

        std::vector<const char *> validation_layers = {
            "VK_LAYER_KHRONOS_validation"};
        std::vector<const char *> device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        float queue_priority = 1.f;
    };
}

#endif