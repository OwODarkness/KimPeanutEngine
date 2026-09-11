#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_COMMAND_RECORDER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_COMMAND_RECORDER_H

#include <functional>
#include <optional>
#include <utility>
#include <vulkan/vulkan.h>

#include "common/command_recorder.h"

namespace kpengine::graphics
{
    class MeshManager;
    class VulkanBufferManager;
    class VulkanDescriptorSetManager;
    class VulkanPipelineManager;
    class VulkanRenderTargetManager;
    class VulkanBindlessTextureTable;
    class VulkanEditorBridge;

    // Valid only between VulkanBackend::BeginFrame and EndFrame. It borrows all
    // services; VulkanBackend remains their owner and controls submission.
    class VulkanCommandRecorder final : public CommandRecorder
    {
    public:
        void Begin(VkCommandBuffer command_buffer, VulkanPipelineManager &pipeline_manager,
                   VulkanDescriptorSetManager &descriptor_set_manager,
                   VulkanBufferManager &buffer_manager, MeshManager &mesh_manager,
                   VulkanRenderTargetManager &render_target_manager,
                   VulkanBindlessTextureTable *bindless_table, uint32_t frame_index,
                   VulkanEditorBridge *presentation_bridge);

        bool BeginRenderTarget(RenderTargetHandle target) override;
        bool BeginPresentation() override;
        void EndRenderTarget() override;
        bool BindPipeline(PipelineHandle pipeline) override;
        void BindMesh(MeshHandle mesh) override;
        bool BindGeometry(const GeometryView &geometry) override;
        bool BindResourceBindings(PipelineHandle pipeline,
                                  DescriptorSetHandle bindings,
                                  const DynamicUniformOffsets &dynamic_offsets = {}) override;

        void SetGeometryBufferResolvers(
            std::function<std::optional<BufferDesc>(BufferHandle)> desc_lookup,
            std::function<BufferHandle(BufferHandle)> handle_lookup)
        {
            get_geometry_buffer_desc_ = std::move(desc_lookup);
            get_geometry_buffer_handle_ = std::move(handle_lookup);
        }
        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const Scissor &scissor) override;
        void DrawIndexed(uint32_t index_count, uint32_t instance_count,
                         uint32_t first_index, int32_t vertex_offset,
                         uint32_t first_instance) override;
        CommandRecorderProfileCounters GetProfileCounters() const override
        {
            return profile_counters_;
        }

    private:
        void ResetStateCache() noexcept;

        VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
        VulkanPipelineManager *pipeline_manager_ = nullptr;
        VulkanDescriptorSetManager *descriptor_set_manager_ = nullptr;
        VulkanBufferManager *buffer_manager_ = nullptr;
        MeshManager *mesh_manager_ = nullptr;
        VulkanRenderTargetManager *render_target_manager_ = nullptr;
        VulkanBindlessTextureTable *bindless_table_ = nullptr;
        VulkanEditorBridge *presentation_bridge_ = nullptr;
        uint32_t frame_index_ = 0;
        uint32_t recorded_index_count_ = 0;
        uint32_t recorded_first_index_ = 0;
        size_t recorded_index_offset_ = 0;
        IndexElementType recorded_index_type_ = IndexElementType::UInt32;
        PipelineHandle recorded_pipeline_;
        MeshHandle recorded_mesh_;
        bool recorded_geometry_ = false;
        PipelineHandle validated_pipeline_;
        RenderTargetHandle validated_target_;
        bool cached_pipeline_compatibility_ = false;
        DescriptorSetHandle recorded_bindings_;
        PipelineHandle recorded_bindings_pipeline_;
        DynamicUniformOffsets recorded_dynamic_offsets_;
        RenderTargetHandle active_target_;
        // Suppresses draws when the bound pipeline's attachment formats do not
        // match the active render target; recording stays pass-scoped instead of
        // submitting a pipeline-state mismatch to the driver.
        bool draws_suppressed_ = false;
        bool presentation_active_ = false;
        std::function<std::optional<BufferDesc>(BufferHandle)> get_geometry_buffer_desc_;
        std::function<BufferHandle(BufferHandle)> get_geometry_buffer_handle_;
        CommandRecorderProfileCounters profile_counters_{};
    };
}

#endif
