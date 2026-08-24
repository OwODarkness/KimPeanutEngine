#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_COMMAND_RECORDER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_COMMAND_RECORDER_H

#include <vulkan/vulkan.h>

#include "common/command_recorder.h"

namespace kpengine::graphics
{
    class MeshManager;
    class VulkanBufferManager;
    class VulkanDescriptorSetManager;
    class VulkanPipelineManager;
    class VulkanRenderTargetManager;

    // Valid only between VulkanBackend::BeginFrame and EndFrame. It borrows all
    // services; VulkanBackend remains their owner and controls submission.
    class VulkanCommandRecorder final : public CommandRecorder
    {
    public:
        void Begin(VkCommandBuffer command_buffer, VulkanPipelineManager &pipeline_manager,
                   VulkanDescriptorSetManager &descriptor_set_manager,
                   VulkanBufferManager &buffer_manager, MeshManager &mesh_manager,
                   VulkanRenderTargetManager &render_target_manager);

        void BeginRenderTarget(RenderTargetHandle target) override;
        void EndRenderTarget() override;
        void BindPipeline(PipelineHandle pipeline) override;
        void BindMesh(MeshHandle mesh) override;
        void BindResourceBindings(PipelineHandle pipeline,
                                  DescriptorSetHandle bindings) override;
        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const Scissor &scissor) override;
        void DrawIndexed(uint32_t index_count, uint32_t instance_count,
                         uint32_t first_index, int32_t vertex_offset,
                         uint32_t first_instance) override;

    private:
        VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
        VulkanPipelineManager *pipeline_manager_ = nullptr;
        VulkanDescriptorSetManager *descriptor_set_manager_ = nullptr;
        VulkanBufferManager *buffer_manager_ = nullptr;
        MeshManager *mesh_manager_ = nullptr;
        VulkanRenderTargetManager *render_target_manager_ = nullptr;
        uint32_t recorded_index_count_ = 0;
    };
}

#endif
