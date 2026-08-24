#include "vulkan_command_recorder.h"

#include "common/mesh.h"
#include "common/mesh_manager.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_descriptor_set_manager.h"
#include "vulkan_mesh.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_render_target_manager.h"

namespace kpengine::graphics
{
    void VulkanCommandRecorder::Begin(
        VkCommandBuffer command_buffer, VulkanPipelineManager &pipeline_manager,
        VulkanDescriptorSetManager &descriptor_set_manager,
        VulkanBufferManager &buffer_manager, MeshManager &mesh_manager,
        VulkanRenderTargetManager &render_target_manager)
    {
        command_buffer_ = command_buffer;
        pipeline_manager_ = &pipeline_manager;
        descriptor_set_manager_ = &descriptor_set_manager;
        buffer_manager_ = &buffer_manager;
        mesh_manager_ = &mesh_manager;
        render_target_manager_ = &render_target_manager;
        recorded_index_count_ = 0;
    }

    void VulkanCommandRecorder::BeginRenderTarget(RenderTargetHandle target)
    {
        if (command_buffer_ != VK_NULL_HANDLE)
        {
            render_target_manager_->BeginRendering(command_buffer_, target);
        }
    }

    void VulkanCommandRecorder::EndRenderTarget()
    {
        if (command_buffer_ != VK_NULL_HANDLE)
        {
            render_target_manager_->EndRendering(command_buffer_);
        }
    }

    void VulkanCommandRecorder::BindPipeline(PipelineHandle pipeline)
    {
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        const VulkanPipelineResource *resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (resource)
        {
            vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, resource->pipeline);
        }
    }

    void VulkanCommandRecorder::BindMesh(MeshHandle mesh)
    {
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        Mesh *mesh_object = mesh_manager_->GetMesh(mesh);
        const auto *mesh_resource = mesh_object ? static_cast<const VulkanMeshResource *>(
            mesh_object->GetMeshHandle().native) : nullptr;
        if (!mesh_resource || mesh_resource->sections.empty())
        {
            return;
        }
        VulkanBufferResource *vertex = buffer_manager_->GetBufferResource(mesh_resource->vertex_handle);
        VulkanBufferResource *index = buffer_manager_->GetBufferResource(mesh_resource->index_handle);
        if (!vertex || !index)
        {
            return;
        }
        const VkBuffer vertex_buffers[] = {vertex->buffer};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffer_, 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffer_, index->buffer, 0, VK_INDEX_TYPE_UINT32);
        recorded_index_count_ = static_cast<uint32_t>(mesh_resource->sections[0].index_count);
    }

    void VulkanCommandRecorder::BindResourceBindings(PipelineHandle pipeline,
                                                       DescriptorSetHandle bindings)
    {
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        const VkDescriptorSet descriptor_set = descriptor_set_manager_->GetDescriptorSet(bindings);
        if (pipeline_resource && descriptor_set != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_resource->layout, 0, 1, &descriptor_set, 0, nullptr);
        }
    }

    void VulkanCommandRecorder::SetViewport(const Viewport &viewport)
    {
        if (command_buffer_ == VK_NULL_HANDLE) return;
        const VkViewport native{viewport.x, viewport.y, viewport.width, viewport.height,
                                viewport.min_depth, viewport.max_depth};
        vkCmdSetViewport(command_buffer_, 0, 1, &native);
    }

    void VulkanCommandRecorder::SetScissor(const Scissor &scissor)
    {
        if (command_buffer_ == VK_NULL_HANDLE) return;
        const VkRect2D native{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
        vkCmdSetScissor(command_buffer_, 0, 1, &native);
    }

    void VulkanCommandRecorder::DrawIndexed(uint32_t index_count, uint32_t instance_count,
                                             uint32_t first_index, int32_t vertex_offset,
                                             uint32_t first_instance)
    {
        if (command_buffer_ == VK_NULL_HANDLE) return;
        const uint32_t count = index_count == 0 ? recorded_index_count_ : index_count;
        if (count != 0)
        {
            vkCmdDrawIndexed(command_buffer_, count, instance_count, first_index,
                             vertex_offset, first_instance);
        }
    }
}
