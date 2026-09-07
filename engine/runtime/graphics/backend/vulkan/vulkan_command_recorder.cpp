#include "vulkan_command_recorder.h"

#include <chrono>

#include "common/mesh.h"
#include "common/mesh_manager.h"
#include "common/render_target_validation.h"
#include "log/logger.h"

#define KP_VULKAN_COMMAND_RECORDER_LOG_NAME "VulkanCommandRecorderLog"
#include "vulkan_buffer_manager.h"
#include "vulkan_bindless_texture_table.h"
#include "vulkan_descriptor_set_manager.h"
#include "vulkan_mesh.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_render_target_manager.h"

namespace kpengine::graphics
{
    void VulkanCommandRecorder::ResetStateCache() noexcept
    {
        recorded_pipeline_ = {};
        recorded_mesh_ = {};
        validated_pipeline_ = {};
        validated_target_ = {};
        cached_pipeline_compatibility_ = false;
        recorded_bindings_ = {};
        recorded_bindings_pipeline_ = {};
        recorded_dynamic_offsets_.clear();
        recorded_index_count_ = 0;
        recorded_first_index_ = 0;
    }

    void VulkanCommandRecorder::Begin(
        VkCommandBuffer command_buffer, VulkanPipelineManager &pipeline_manager,
        VulkanDescriptorSetManager &descriptor_set_manager,
        VulkanBufferManager &buffer_manager, MeshManager &mesh_manager,
        VulkanRenderTargetManager &render_target_manager,
        VulkanBindlessTextureTable *bindless_table, uint32_t frame_index)
    {
        command_buffer_ = command_buffer;
        pipeline_manager_ = &pipeline_manager;
        descriptor_set_manager_ = &descriptor_set_manager;
        buffer_manager_ = &buffer_manager;
        mesh_manager_ = &mesh_manager;
        render_target_manager_ = &render_target_manager;
        bindless_table_ = bindless_table;
        frame_index_ = frame_index;
        active_target_ = {};
        draws_suppressed_ = false;
        ResetStateCache();
        profile_counters_ = {};
    }

    bool VulkanCommandRecorder::BeginRenderTarget(RenderTargetHandle target)
    {
        if (command_buffer_ == VK_NULL_HANDLE || !render_target_manager_ ||
            active_target_.IsValid())
        {
            draws_suppressed_ = true;
            return false;
        }

        draws_suppressed_ = false;
        if (!render_target_manager_->BeginRendering(command_buffer_, target))
        {
            draws_suppressed_ = true;
            return false;
        }
        active_target_ = target;
        ResetStateCache();
        return true;
    }

    void VulkanCommandRecorder::EndRenderTarget()
    {
        if (command_buffer_ != VK_NULL_HANDLE)
        {
            render_target_manager_->EndRendering(command_buffer_);
        }
        active_target_ = {};
        ResetStateCache();
    }

    void VulkanCommandRecorder::BindPipeline(PipelineHandle pipeline)
    {
        ++profile_counters_.pipeline_bind_requests;
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        const VulkanPipelineResource *resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (!resource)
        {
            draws_suppressed_ = true;
            return;
        }
        if (recorded_pipeline_ == pipeline && !draws_suppressed_)
        {
            return;
        }

        const bool validation_cached = validated_pipeline_ == pipeline &&
                                       validated_target_ == active_target_;
        if (validation_cached && !cached_pipeline_compatibility_)
        {
            draws_suppressed_ = true;
            return;
        }

        const RenderTargetDesc *target_desc =
            render_target_manager_ ? render_target_manager_->GetDesc(active_target_) : nullptr;
        bool compatible = true;
        if (!validation_cached && target_desc)
        {
            const auto validation_started = std::chrono::steady_clock::now();
            PipelineDesc pipeline_desc{};
            pipeline_desc.color_attachment_formats = resource->color_attachment_formats;
            pipeline_desc.depth_attachment_format = resource->depth_attachment_format;
            pipeline_desc.multisample_state.rasterization_samples =
                resource->rasterization_samples;
            std::string error;
            compatible =
                ValidateRenderTargetPipelineCompatibility(*target_desc, pipeline_desc, &error);
            ++profile_counters_.pipeline_validation_calls;
            profile_counters_.pipeline_validation_cpu_ms +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - validation_started)
                    .count();
            if (!compatible)
            {
                KP_LOG(KP_VULKAN_COMMAND_RECORDER_LOG_NAME, LOG_LEVEL_ERROR,
                       "Rejected pipeline for incompatible render target: %s", error.c_str());
            }
        }
        validated_pipeline_ = pipeline;
        validated_target_ = active_target_;
        cached_pipeline_compatibility_ = compatible;
        if (!compatible)
        {
            draws_suppressed_ = true;
            return;
        }
        draws_suppressed_ = false;
        {
            vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, resource->pipeline);
            if (bindless_table_ && bindless_table_->IsReady())
            {
                const VkDescriptorSet descriptor_set = bindless_table_->GetDescriptorSet(frame_index_);
                if (descriptor_set != VK_NULL_HANDLE)
                {
                    vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            resource->layout,
                                            BindlessTextureTableLayout::descriptor_set, 1,
                                            &descriptor_set, 0, nullptr);
                }
            }
            recorded_pipeline_ = pipeline;
            recorded_mesh_ = {};
            recorded_bindings_ = {};
            recorded_bindings_pipeline_ = {};
            recorded_dynamic_offsets_.clear();
            ++profile_counters_.pipeline_bind_emitted;
        }
    }

    void VulkanCommandRecorder::BindMesh(MeshHandle mesh)
    {
        ++profile_counters_.mesh_bind_requests;
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        if (recorded_mesh_ == mesh && recorded_pipeline_.IsValid())
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
        recorded_first_index_ = static_cast<uint32_t>(mesh_resource->sections[0].index_start);
        recorded_mesh_ = mesh;
        ++profile_counters_.mesh_bind_emitted;
    }

    void VulkanCommandRecorder::BindResourceBindings(PipelineHandle pipeline,
                                                       DescriptorSetHandle bindings,
                                                       const DynamicUniformOffsets &dynamic_offsets)
    {
        ++profile_counters_.resource_binding_bind_requests;
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            return;
        }
        if (recorded_bindings_pipeline_ == pipeline && recorded_bindings_ == bindings &&
            recorded_dynamic_offsets_ == dynamic_offsets)
        {
            return;
        }
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        const VkDescriptorSet descriptor_set = descriptor_set_manager_->GetDescriptorSet(bindings);
        if (pipeline_resource && descriptor_set != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_resource->layout, 0, 1, &descriptor_set,
                                    static_cast<uint32_t>(dynamic_offsets.size()),
                                    dynamic_offsets.empty() ? nullptr : dynamic_offsets.data());
            recorded_bindings_pipeline_ = pipeline;
            recorded_bindings_ = bindings;
            recorded_dynamic_offsets_ = dynamic_offsets;
            ++profile_counters_.resource_binding_bind_emitted;
        }
    }

    void VulkanCommandRecorder::SetViewport(const Viewport &viewport)
    {
        if (command_buffer_ == VK_NULL_HANDLE) return;
        const VkViewport native{viewport.x, viewport.y + viewport.height,
                                viewport.width, -viewport.height,
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
        if (command_buffer_ == VK_NULL_HANDLE || draws_suppressed_) return;
        const uint32_t count = index_count == 0 ? recorded_index_count_ : index_count;
        const uint32_t first = index_count == 0 ? recorded_first_index_ : first_index;
        if (count != 0)
        {
            vkCmdDrawIndexed(command_buffer_, count, instance_count, first,
                             vertex_offset, first_instance);
            ++profile_counters_.draw_calls_emitted;
        }
    }
}
