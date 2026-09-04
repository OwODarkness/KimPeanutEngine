#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BACKEND_H

#include <cstdint>
#include <memory>
#include <vulkan/vulkan.h>

#include "math/math_header.h"
#include "common/render_backend.h"
#include "common/texture.h"
#include "vulkan_context.h"
#include "vulkan_command_recorder.h"
#include "vulkan_device.h"

namespace kpengine::graphics
{
    struct VulkanPipelineResource;

    class VulkanBackend : public RenderBackend
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
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &desc) override;
        bool DestroyRenderTarget(RenderTargetHandle handle) override;
        TextureHandle GetRenderTargetColor(RenderTargetHandle handle) override;
        TextureHandle GetRenderTargetColorAttachment(RenderTargetHandle handle,
                                                     uint32_t index) override;
        TextureHandle GetRenderTargetDepthAttachment(RenderTargetHandle handle) override;
        TextureHandle GetRenderTargetSampledDepthAttachment(RenderTargetHandle handle) override;
        RenderTargetView GetRenderTargetView(RenderTargetHandle handle) override;
        IRenderTargetReadback *GetRenderTargetReadback() override;
        DescriptorSetHandle CreateResourceBindingSet(
            PipelineHandle pipeline, const ResourceBindingSetDesc &desc) override;
        bool DestroyResourceBindingSet(DescriptorSetHandle handle) override;
        BindlessTextureHandle AcquireBindlessTexture(TextureHandle texture,
                                                     SamplerHandle sampler) override;
        bool ReleaseBindlessTexture(BindlessTextureHandle handle) override;
        void BindResourceBindingSet(PipelineHandle pipeline,
                                    DescriptorSetHandle handle) override;
        virtual void BeginFrame() override;
        CommandRecorder *GetCommandRecorder() override;
        virtual void EndFrame() override;
        GraphicsAPIType GetGraphicsAPI() const override
        {
            return GraphicsAPIType::GRAPHICS_API_VULKAN;
        }
        IEditorPresentationBridge *GetEditorPresentationBridge() override;
        BufferHandle CreateUniformBuffer(uint32_t size) override;
        void *MapUniformBuffer(BufferHandle handle, size_t size) override;
        uint32_t GetCurrentFrameIndex() const override;
        uint32_t GetFramesInFlight() const override;
        size_t GetUniformBufferAlignment() const override;
        Extent2D GetRenderExtent() const override;
        void WaitIdle() override;
        virtual void Cleanup() override;

        BufferHandle CreateVertexBuffer(const void *data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void *data, size_t size) override;
        bool DestroyBufferResource(BufferHandle handle) override;

    private:
        void InitializeCapabilities();
        void InitVulkanContext();
        GraphicsContext CreateGraphicsContext() const;
        BufferHandle CreateBuffer(const void *data, size_t size, VkBufferUsageFlags usage);
        void UploadTexturePixels(TextureHandle texture, const void *pixels, size_t pixel_size,
                                 uint32_t width, uint32_t height, uint32_t mip_levels);

        void FinishFrame(VkCommandBuffer commandbuffer, uint32_t image_index);

        void RecreateSwapchain();
        void CleanupSwapchain();
        void FramebufferResizeCallback(const ResizeEvent &event) override;

    private:
        std::unique_ptr<class VulkanDevice> device_;
        std::unique_ptr<class VulkanSwapchain> swapchain_;
        std::unique_ptr<class VulkanFrameContext> frame_context_;
        std::unique_ptr<class VulkanCommandRecorder> command_recorder_;
        std::unique_ptr<class VulkanRenderTargetManager> render_target_manager_;
        std::unique_ptr<class VulkanRenderTargetReadback> render_target_readback_;
        std::unique_ptr<class VulkanEditorBridge> editor_bridge_;
        VulkanContext context_;

        std::unique_ptr<class VulkanMemoryManager> memory_manager_;
        std::unique_ptr<class VulkanBufferManager> buffer_manager_;
        std::unique_ptr<class VulkanUploadContext> upload_context_;

        std::unique_ptr<class VulkanPipelineManager> pipeline_manager_;
        std::unique_ptr<class VulkanDescriptorSetManager> descriptor_set_manager_;
        std::unique_ptr<class VulkanBindlessTextureTable> bindless_texture_table_;

        std::unique_ptr<class VulkanImageMemoryManager> image_memory_manager_;

        std::unique_ptr<class TextureManager> texture_manager_;
        std::unique_ptr<class SamplerManager> sampler_manager_;
        std::unique_ptr<class MeshManager> mesh_manager_;

        uint32_t msaa_sampe_count_ = 1;
        uint32_t current_image_index_ = 0;
        bool frame_active_ = false;
    };
}

#endif
