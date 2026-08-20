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

    class VulkanBackend : public RenderBackend, public CommandRecorder
    {
    public:
        VulkanBackend();
        ~VulkanBackend();
        virtual void Initialize(WindowHandle native_window) override;
        PipelineHandle CreatePipelineResource(const PipelineDesc &pipeline_desc) override;
        bool DestroyPipelineResource(PipelineHandle handle) override;
        MeshHandle CreateMesh(const data::MeshData &data) override;
        bool DestroyMesh(MeshHandle handle) override;
        TextureHandle CreateTexture(const data::TextureData &data,
                                    const TextureSettings &settings) override;
        bool DestroyTexture(TextureHandle handle) override;
        SamplerHandle CreateSampler(const SamplerSettings &settings) override;
        bool DestroySampler(SamplerHandle handle) override;
        DescriptorSetHandle CreateResourceBindingSet(
            PipelineHandle pipeline, const ResourceBindingSetDesc &desc) override;
        bool DestroyResourceBindingSet(DescriptorSetHandle handle) override;
        void BindResourceBindingSet(PipelineHandle pipeline,
                                    DescriptorSetHandle handle) override;
        virtual void BeginFrame() override;
        CommandRecorder *GetCommandRecorder() override;
        void BindPipeline(PipelineHandle pipeline) override;
        void BindMesh(MeshHandle mesh) override;
        void BindResourceBindings(PipelineHandle pipeline,
                                   DescriptorSetHandle bindings) override;
        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const Scissor &scissor) override;
        void DrawIndexed(uint32_t index_count, uint32_t instance_count,
                         uint32_t first_index, int32_t vertex_offset,
                         uint32_t first_instance) override;
        virtual void EndFrame() override;
        BufferHandle CreateUniformBuffer(uint32_t size) override;
        void *MapUniformBuffer(BufferHandle handle, size_t size) override;
        uint32_t GetCurrentFrameIndex() const override;
        uint32_t GetFramesInFlight() const override;
        size_t GetUniformBufferAlignment() const override;
        Extent2D GetRenderExtent() const override;
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
        void UploadTexturePixels(TextureHandle texture, const void *pixels, size_t pixel_size, uint32_t width, uint32_t height, uint32_t mip_levels);

        class VulkanImageMemoryManager *GetImageMemoryManager() const { return image_memory_manager_.get(); }
        VkCommandBuffer GetCurrentUICommandBuffer() const;

        // Frame-recording API: BeginFrame prepares the frame's scene command
        // buffer (acquisition, attachment transitions, rendering begun); the
        // caller records draws into it, then EndFrame submits and presents.
        VkCommandBuffer GetCurrentSceneCommandBuffer() const;
        uint32_t GetCurrentImageIndex() const { return current_image_index_; }

        const VulkanPipelineResource *GetPipelineResource(PipelineHandle handle) const;
        VulkanContext &GetVulkanContext() { return context_; }
        class TextureManager *GetTextureManager() const { return texture_manager_.get(); }
        class SamplerManager *GetSamplerManager() const { return sampler_manager_.get(); }
        class MeshManager *GetMeshManager() const { return mesh_manager_.get(); }

    private:
        void InitVulkanContext();
        GraphicsContext CreateGraphicsContext() const;
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
        std::unique_ptr<class VulkanDescriptorSetManager> descriptor_set_manager_;

        std::unique_ptr<class VulkanImageMemoryManager> image_memory_manager_;

        std::unique_ptr<class TextureManager> texture_manager_;
        std::unique_ptr<class SamplerManager> sampler_manager_;
        std::unique_ptr<class MeshManager> mesh_manager_;

        TextureHandle depth_handle_;
        TextureHandle color_handle_;

        uint32_t msaa_sampe_count_ = 1;
        uint32_t current_image_index_ = 0;
        bool frame_active_ = false;
        MeshHandle recorded_mesh_;
        uint32_t recorded_index_count_ = 0;
    };
}

#endif
