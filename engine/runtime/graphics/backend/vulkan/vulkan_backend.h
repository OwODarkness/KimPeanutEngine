#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H

#include <vector>
#include <cstdint>
#include <memory>
#include <vulkan/vulkan.h>

#include "math/math_header.h"
#include "common/render_backend.h"
#include "common/texture.h"
#include "vulkan_context.h"
#include "vulkan_device.h"



namespace kpengine::graphics
{
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
        virtual void Initialize(const PipelineDesc &pipeline_desc) override;
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

        void CopyBufferToImage(VkCommandBuffer cmd, BufferHandle handle, VkImage image, uint32_t width, uint32_t height);
        class VulkanImageMemoryManager *GetImageMemoryManager() const { return image_memory_manager_.get(); }
        VkCommandBuffer GetCurrentUICommandBuffer() const;

    private:
        void InitVulkanContext();
        void CreateGraphicsPipeline(const PipelineDesc &pipeline_desc);
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

        void RecreateSwapchain();
        void CleanupSwapchain();
        void DestroyAttachmentResources();

    private:
        void RecordCommandBuffer(VkCommandBuffer commandbuffer, uint32_t image_index);

        void FramebufferResizeCallback(const ResizeEvent &event) override;

    private:
        std::unique_ptr<class VulkanDevice> device_;
        std::unique_ptr<class VulkanSwapchain> swapchain_;
        std::unique_ptr<class VulkanFrameContext> frame_context_;
        VulkanContext context_;

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
    };
}

#endif
