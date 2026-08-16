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
    struct VulkanPipelineResource;

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

        // Scene-side resource facilities. The caller (today: the demo scene in the
        // render module) composes these; the backend only provides the RHI pieces.
        BufferHandle CreateUniformBuffer(uint32_t size);
        void *MapUniformBuffer(BufferHandle handle, size_t size);
        void UploadTexturePixels(TextureHandle texture, const void *pixels, size_t pixel_size, uint32_t width, uint32_t height, uint32_t mip_levels);

        class VulkanImageMemoryManager *GetImageMemoryManager() const { return image_memory_manager_.get(); }
        VkCommandBuffer GetCurrentUICommandBuffer() const;

        // Frame-recording API: BeginFrame prepares the frame's scene command
        // buffer (acquisition, attachment transitions, rendering begun); the
        // caller records draws into it, then EndFrame submits and presents.
        VkCommandBuffer GetCurrentSceneCommandBuffer() const;
        uint32_t GetCurrentFrameIndex() const;
        uint32_t GetCurrentImageIndex() const { return current_image_index_; }
        VkExtent2D GetSwapchainExtent() const;

        const VulkanPipelineResource *GetPipelineResource() const;
        VulkanContext &GetVulkanContext() { return context_; }
        class TextureManager *GetTextureManager() const { return texture_manager_.get(); }
        class SamplerManager *GetSamplerManager() const { return sampler_manager_.get(); }
        class MeshManager *GetMeshManager() const { return mesh_manager_.get(); }

    private:
        void InitVulkanContext();
        void CreateGraphicsPipeline(const PipelineDesc &pipeline_desc);
        BufferHandle CreateBuffer(const void *data, size_t size, VkBufferUsageFlags usage);

        // Swapchain-bound render targets — RHI-owned, sized to the swapchain.
        void CreateDepthResource();
        void CreateColorResource();

        // frame skeleton: transitions + dynamic-rendering begin/end around the
        // caller's draws
        void BeginSceneFrame(VkCommandBuffer commandbuffer, uint32_t image_index);
        void EndSceneFrame(VkCommandBuffer commandbuffer, uint32_t image_index);

        void RecreateSwapchain();
        void CleanupSwapchain();
        void DestroyAttachmentResources();

        void FramebufferResizeCallback(const ResizeEvent &event) override;

    private:
        std::unique_ptr<class VulkanDevice> device_;
        std::unique_ptr<class VulkanSwapchain> swapchain_;
        std::unique_ptr<class VulkanFrameContext> frame_context_;
        VulkanContext context_;

        std::unique_ptr<class VulkanBufferManager> buffer_manager_;

        std::unique_ptr<class VulkanPipelineManager> pipeline_manager_;
        PipelineHandle pipeline_handle_;

        std::unique_ptr<class VulkanImageMemoryManager> image_memory_manager_;

        std::unique_ptr<class TextureManager> texture_manager_;
        std::unique_ptr<class SamplerManager> sampler_manager_;
        std::unique_ptr<class MeshManager> mesh_manager_;

        TextureHandle depth_handle_;
        TextureHandle color_handle_;

        uint32_t msaa_sampe_count_ = 1;
        uint32_t current_image_index_ = 0;
        bool frame_active_ = false;
    };
}

#endif
