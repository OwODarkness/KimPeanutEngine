#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H

#include <vector>
#include <cstdint>
#include <optional>
#include <vulkan/vulkan.h>

#include "math/math_header.h"
#include "common/render_backend.h"
#include "vulkan_buffer_pool.h"
#include "vulkan_pipeline_manager.h"

namespace kpengine::graphics
{
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphics_family;
        std::optional<uint32_t> present_family;
        std::optional<uint32_t> transfer_family;
        bool IsComplete() const
        {
            return graphics_family.has_value() && present_family.has_value() && transfer_family.has_value(); 
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

    struct VulkanQueue{
        uint32_t index = UINT_MAX;
        VkQueue queue;
    };


    struct UniformBufferObject{
        Matrix4f model;
        Matrix4f view;
        Matrix4f proj;
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
        void CreateCommandPools();
        void CreateCommandBuffers();
        void CreateSyncObjects();
        void CreateVertexBuffers();
        BufferHandle CreateBuffer(const void* data, size_t size, VkBufferUsageFlags usage);
        
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void UpdateUniformBuffer(uint32_t current_image);

        void CopyBuffer(BufferHandle src_handle, BufferHandle dst_handle, VkDeviceSize size);
        void RecreateSwapchain();
        void CleanupSwapchain();
    private:
        void RecordCommandBuffer(VkCommandBuffer commandbuffer, uint32_t image_index);
        void FramebufferResizeCallback(const ResizeEvent& event) override;

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

        VulkanQueue graphics_queue_;
        VulkanQueue present_queue_;
        VulkanQueue transfer_queue_;

        VkSwapchainKHR swapchain_;
        VkExtent2D resolution_;
        VkFormat swapchain_image_format_;
        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_imageviews_;
        VkRenderPass swapchain_renderpass_;
        std::vector<VkFramebuffer> swapchain_framebuffers_;
        
        VkCommandPool graphics_command_pool_;
        VkCommandPool transfer_command_pool_;

        std::vector<VkCommandBuffer> command_buffers_;
        std::vector<VkSemaphore> available_image_sepmaphores_;
        std::vector<VkSemaphore> render_finished_semaphores_;
        std::vector<VkFence> in_flight_fences_;

        uint32_t current_frame = 0;
        bool has_resized = false;

        VulkanBufferPool buffer_pool_;
        BufferHandle pos_handle_;
        BufferHandle color_handle_;
        BufferHandle index_handle_;

        std::vector<BufferHandle> uniform_buffer_handles_;
        std::vector<void*> uniform_buffer_mapped_ptr_;

        VulkanPipelineManager pipeline_manager_;
        PipelineHandle pipeline_handle;

        VkDescriptorPool descriptor_pool_;
        std::vector<VkDescriptorSet> descriptor_sets_;

        std::vector<const char *> validation_layers = {
            "VK_LAYER_KHRONOS_validation"};
        std::vector<const char *> device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        float queue_priority = 1.f;
    };
}

#endif