#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H

#include <vector>
#include <cstdint>
#include <optional>
#include <memory>
#include <vulkan/vulkan.h>
#include "math/math_header.h"
#include "common/render_backend.h"
#include "vulkan_context.h"
#include "common/texture.h"



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

    struct VulkanQueue
    {
        uint32_t index = UINT_MAX;
        VkQueue queue;
    };

    struct UniformBufferData
    {
        std::vector<BufferHandle> buffer_handles_;
        std::vector<void *> buffer_mapped_ptr_;
        uint32_t element_size;
    };

    class VulkanBackend : public RenderBackend
    {
    public:
        VulkanBackend();
        ~VulkanBackend();
        virtual void Initialize() override;
        virtual void BeginFrame() override;
        virtual void EndFrame() override;
        virtual void Present() override;
        virtual void Cleanup() override;

        BufferHandle CreateUploadStageBufferResource(size_t size);
        BufferHandle CreateDownloadStageBufferResource(size_t size);

        BufferHandle CreateVertexBuffer(const void *data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void *data, size_t size) override;
        bool DestroyBufferResource(BufferHandle handle) override;
        void UploadDataToBuffer(BufferHandle handle, size_t size, const void *data);
        struct VulkanBufferResource *GetBufferResource(BufferHandle handle);

        void TransitionImageLayout(VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count);
        void ReleaseImageOwnerShip(VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count);
        void AcquireImageOwnerShip(VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count);

        /**
         * runtime mipmaps generate function.
         * Note: Usually they are pre-generated and stored in the texture file alongside the base level to improve loading speed.
         *
         */
        void GenerateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mip_levels);

        void CopyBufferToImage(BufferHandle handle, VkImage image, uint32_t width, uint32_t height);
        class VulkanImageMemoryManager *GetImageMemoryManager() const { return image_memory_manager_.get(); }
        VkCommandBuffer GetCurrentUICommandBuffer() const;

    private:
        void CreateInstance();
        void CreateDebugMessager();
        void CreateSurface();
        void CreatePhysicalDevice();
        void CreateLogicalDevice();
        void InitVulkanContext();
        void CreateSwapchain();
        void CreateSwapchainImageViews();
        void CreateSwapchainRenderPass();
        void CreateGraphicsPipeline();
        void CreateFrameBuffers();
        void CreateCommandPools();
        void CreateCommandBuffers();
        void CreateSyncObjects();
        void CreateVertexBuffers();
        BufferHandle CreateBuffer(const void *data, size_t size, VkBufferUsageFlags usage);
        void CreateTextures(TextureData& data);

        void CreateUniformBuffers();
        void CreateUniformBuffer(uint32_t size, uint32_t element_count, UniformBufferData& ubo_data);

        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void WriteUniformBufferDescriptorSet(VkWriteDescriptorSet& out,VkDescriptorBufferInfo & desc_info, VkDescriptorSet descriptor_set, const UniformBufferData& ubo_data, VkDescriptorSetLayoutBinding binding, uint32_t frame_index);
        void WriteImageDescriptorSet(VkWriteDescriptorSet& out, VkDescriptorImageInfo& image_info, VkDescriptorSet descriptor_set, TextureHandle texture_handle, SamplerHandle sampler_handle, VkDescriptorSetLayoutBinding binding, uint32_t frame_index);

        void SetupResource();
        void CreateDepthResource();
        void CreateColorResource();
        void UpdateUniformBuffer(uint32_t current_image);
        void CopyToUniformBuffer(void* buffer_mapped_ptr,  const void* data, uint32_t size);

        void TransitionImageLayout(VkImage image, VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access, VkImageAspectFlags aspect_mask, uint32_t base_mip_level, uint32_t level_count);
        void TransitionImageLayout2(VkImage image, VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 src_access, VkAccessFlags2 dst_access, VkImageAspectFlags aspect_mask, uint32_t base_mip_level, uint32_t level_count);
        void ReleaseImageOwnerShip(VkImage image, VkImageLayout current_layout, uint32_t src_queue_family, uint32_t dst_queue_family, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkImageAspectFlags aspect_mask, VkCommandPool command_pool, VkQueue queue, uint32_t base_mip_level, uint32_t level_count);
        void AcquireImageOwnerShip(VkImage image, VkImageLayout expected_layout, uint32_t src_queue_family, uint32_t dst_queue_family, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access, VkImageAspectFlags aspect_mask, VkCommandPool command_pool, VkQueue queue, uint32_t base_mip_level, uint32_t level_count);

        void CopyBuffer(BufferHandle src_handle, BufferHandle dst_handle, VkDeviceSize size);
        void RecreateSwapchain();
        void CleanupSwapchain();

    private:
        void TransferBufferOwnership(BufferHandle buffer_handle, uint32_t src_queue_family, uint32_t dst_queue_family);

        void RecordCommandBuffer(VkCommandBuffer commandbuffer, uint32_t image_index);

        VkCommandBuffer BeginSingleTimeCommands(VkCommandPool commandpool);
        void EndSingleTimeCommands(VkCommandBuffer commandbuffer, VkCommandPool commandpool, VkQueue queue);

        void FramebufferResizeCallback(const ResizeEvent &event) override;

    private:
        std::vector<const char *> FindRequiredExtensions() const;
        bool CheckValidationLayerSupport(const std::vector<const char *> &validation_layers) const;
        bool CheckDeviceExtensionsSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions) const;
        bool CheckPhysicalDeviceSuitable(VkPhysicalDevice device) const;

        VkSurfaceFormatKHR ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) const;
        VkPresentModeKHR ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR> &available_modes) const;
        VkExtent2D ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR &capacity) const;
        uint32_t GetMaxUsableSampleCount();

    private:
        VkInstance instance_;
        VkDebugUtilsMessengerEXT debug_messager_;
        VkSurfaceKHR surface_;
        VkPhysicalDevice physical_device_;
        VkDevice logical_device_;
        VulkanContext context_;

        VulkanQueue graphics_queue_;
        VulkanQueue present_queue_;
        VulkanQueue transfer_queue_;

        VkSwapchainKHR swapchain_;
        VkExtent2D resolution_;
        VkFormat swapchain_image_format_;
        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_imageviews_;
        // VkRenderPass swapchain_renderpass_;
        // std::vector<VkFramebuffer> swapchain_framebuffers_;

        VkCommandPool graphics_command_pool_;
        VkCommandPool transfer_command_pool_;

        VkCommandBuffer dst_command_buffer_;
        VkCommandBuffer shader_command_buffer_;
        VkCommandBuffer copy_command_buffer_;
        VkSemaphore transition_dst_semaphore_;
        VkSemaphore copy_semaphore_;

        std::vector<VkCommandBuffer> scene_command_buffers_;
        std::vector<VkCommandBuffer> ui_command_buffers_;
        std::vector<VkSemaphore> available_image_semaphores_;
        std::vector<VkSemaphore> render_finished_semaphores_;
        std::vector<VkFence> in_flight_fences_;

        uint32_t current_frame_index = 0;
        bool has_resized = false;

        std::unique_ptr<class VulkanBufferManager> buffer_manager_;

        UniformBufferData per_pass_ubo_;
        UniformBufferData per_object_ubo_;

        std::unique_ptr<class VulkanPipelineManager> pipeline_manager_;
        PipelineHandle pipeline_handle_;

        std::unique_ptr<class VulkanImageMemoryManager> image_memory_manager_;

        std::unique_ptr<class TextureManager> texture_manager_;

        TextureHandle texture_handle_;
        TextureHandle depth_handle_;
        TextureHandle color_handle_;

        std::unique_ptr<class SamplerManager> sampler_manager_;
        SamplerHandle sampler_handle_;

        std::unique_ptr<class MeshManager> mesh_manager_;
        MeshHandle mesh_handle_;

        VkDescriptorPool descriptor_pool_;
        std::vector<VkDescriptorSet> descriptor_sets_;

        uint32_t msaa_sampe_count_ = 1;

        std::vector<const char *> validation_layers = {
            "VK_LAYER_KHRONOS_validation"};
        std::vector<const char *> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        float queue_priority = 1.f;
    };
}

#endif