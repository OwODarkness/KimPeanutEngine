#include "vulkan_command_recorder.h"

#include <algorithm>
#include <chrono>
#include <string>

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
#include "vulkan_editor_bridge.h"

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
        recorded_index_offset_ = 0;
        recorded_index_type_ = IndexElementType::UInt32;
        recorded_geometry_ = false;
    }

    void VulkanCommandRecorder::Begin(
        VkCommandBuffer command_buffer, VulkanPipelineManager &pipeline_manager,
        VulkanDescriptorSetManager &descriptor_set_manager,
        VulkanBufferManager &buffer_manager, MeshManager &mesh_manager,
        VulkanRenderTargetManager &render_target_manager,
        VulkanBindlessTextureTable *bindless_table, uint32_t frame_index,
        VulkanEditorBridge *presentation_bridge)
    {
        command_buffer_ = command_buffer;
        pipeline_manager_ = &pipeline_manager;
        descriptor_set_manager_ = &descriptor_set_manager;
        buffer_manager_ = &buffer_manager;
        mesh_manager_ = &mesh_manager;
        render_target_manager_ = &render_target_manager;
        bindless_table_ = bindless_table;
        frame_index_ = frame_index;
        presentation_bridge_ = presentation_bridge;
        presentation_active_ = false;
        active_target_ = {};
        draws_suppressed_ = false;
        ResetStateCache();
        profile_counters_ = {};
    }

    bool VulkanCommandRecorder::BeginRenderTarget(RenderTargetHandle target)
    {
        if (command_buffer_ == VK_NULL_HANDLE || !render_target_manager_ ||
            active_target_.IsValid() || presentation_active_)
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

    bool VulkanCommandRecorder::BeginPresentation()
    {
        if (command_buffer_ == VK_NULL_HANDLE || active_target_.IsValid() ||
            presentation_active_ || presentation_bridge_ == nullptr ||
            !presentation_bridge_->BeginPresentation())
        {
            draws_suppressed_ = true;
            return false;
        }
        presentation_active_ = true;
        draws_suppressed_ = false;
        ResetStateCache();
        return true;
    }

    void VulkanCommandRecorder::EndRenderTarget()
    {
        if (presentation_active_)
        {
            if (presentation_bridge_ != nullptr)
            {
                presentation_bridge_->EndPresentation();
            }
            presentation_active_ = false;
            draws_suppressed_ = false;
            ResetStateCache();
            return;
        }
        if (command_buffer_ != VK_NULL_HANDLE)
        {
            render_target_manager_->EndRendering(command_buffer_);
        }
        active_target_ = {};
        ResetStateCache();
    }

    bool VulkanCommandRecorder::BindPipeline(PipelineHandle pipeline)
    {
        ++profile_counters_.pipeline_bind_requests;
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            draws_suppressed_ = true;
            return false;
        }
        const VulkanPipelineResource *resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (!resource)
        {
            draws_suppressed_ = true;
            ResetStateCache();
            return false;
        }
        if (recorded_pipeline_ == pipeline && !draws_suppressed_)
        {
            return true;
        }

        const bool validation_cached = validated_pipeline_ == pipeline &&
                                       validated_target_ == active_target_;
        if (validation_cached && !cached_pipeline_compatibility_)
        {
            draws_suppressed_ = true;
            return false;
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
            ResetStateCache();
            return false;
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
        return true;
    }

    void VulkanCommandRecorder::BindMesh(MeshHandle mesh)
    {
        ++profile_counters_.mesh_bind_requests;
        const auto reject = [this]()
        {
            recorded_mesh_ = {};
            recorded_geometry_ = false;
            recorded_index_count_ = 0;
            recorded_first_index_ = 0;
            recorded_index_offset_ = 0;
            draws_suppressed_ = true;
        };
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            reject();
            return;
        }
        if (recorded_mesh_ == mesh && recorded_pipeline_.IsValid() &&
            !recorded_geometry_ && !draws_suppressed_)
        {
            return;
        }
        Mesh *mesh_object = mesh_manager_->GetMesh(mesh);
        const auto *mesh_resource = mesh_object ? static_cast<const VulkanMeshResource *>(
            mesh_object->GetMeshHandle().native) : nullptr;
        if (!mesh_resource || mesh_resource->sections.empty())
        {
            reject();
            return;
        }
        VulkanBufferResource *vertex = buffer_manager_->GetBufferResource(mesh_resource->vertex_handle);
        VulkanBufferResource *index = buffer_manager_->GetBufferResource(mesh_resource->index_handle);
        if (!vertex || !index)
        {
            reject();
            return;
        }
        const VkBuffer vertex_buffers[] = {vertex->buffer};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffer_, 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffer_, index->buffer, 0, VK_INDEX_TYPE_UINT32);
        recorded_index_count_ = static_cast<uint32_t>(mesh_resource->sections[0].index_count);
        recorded_first_index_ = static_cast<uint32_t>(mesh_resource->sections[0].index_start);
        recorded_index_offset_ = 0;
        recorded_index_type_ = IndexElementType::UInt32;
        recorded_geometry_ = false;
        recorded_mesh_ = mesh;
        draws_suppressed_ = false;
        ++profile_counters_.mesh_bind_emitted;
    }

    bool VulkanCommandRecorder::BindGeometry(const GeometryView &geometry)
    {
        ++profile_counters_.mesh_bind_requests;
        VulkanPipelineResource *const pipeline = pipeline_manager_
                                                     ? pipeline_manager_->GetPipelineResource(
                                                           recorded_pipeline_)
                                                     : nullptr;
        if (!pipeline || !buffer_manager_ || !get_geometry_buffer_desc_ ||
            !get_geometry_buffer_handle_)
        {
            recorded_mesh_ = {};
            recorded_geometry_ = false;
            draws_suppressed_ = true;
            return false;
        }

        std::string error;
        if (!ValidateGeometryView(geometry, pipeline->binding_descs,
                                  get_geometry_buffer_desc_, &error))
        {
            KP_LOG(KP_VULKAN_COMMAND_RECORDER_LOG_NAME, LOG_LEVEL_ERROR,
                   "Rejected geometry view: %s", error.c_str());
            recorded_mesh_ = {};
            recorded_geometry_ = false;
            recorded_index_count_ = 0;
            recorded_first_index_ = 0;
            draws_suppressed_ = true;
            return false;
        }

        for (const VertexBufferView &view : geometry.vertices)
        {
            const BufferHandle native_handle = get_geometry_buffer_handle_(view.buffer);
            VulkanBufferResource *const vertex =
                buffer_manager_->GetBufferResource(native_handle);
            const auto binding_it = std::find_if(
                pipeline->binding_descs.begin(), pipeline->binding_descs.end(),
                [&view](const VertexBindingDesc &binding) {
                    return binding.binding == view.binding;
                });
            if (!vertex || binding_it == pipeline->binding_descs.end())
            {
                recorded_mesh_ = {};
                recorded_geometry_ = false;
                draws_suppressed_ = true;
                return false;
            }
            const VkBuffer buffer = vertex->buffer;
            const VkDeviceSize offset = static_cast<VkDeviceSize>(view.offset);
            vkCmdBindVertexBuffers(command_buffer_, view.binding, 1, &buffer, &offset);
        }

        const BufferHandle native_index_handle = get_geometry_buffer_handle_(geometry.indices.buffer);
        VulkanBufferResource *const index = buffer_manager_->GetBufferResource(native_index_handle);
        if (!index)
        {
            recorded_mesh_ = {};
            recorded_geometry_ = false;
            draws_suppressed_ = true;
            return false;
        }
        const VkIndexType index_type = geometry.indices.type == IndexElementType::UInt16
                                           ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(command_buffer_, index->buffer,
                             static_cast<VkDeviceSize>(geometry.indices.offset), index_type);
        recorded_mesh_ = {};
        recorded_geometry_ = true;
        recorded_index_type_ = geometry.indices.type;
        recorded_index_count_ = 0;
        recorded_first_index_ = 0;
        recorded_index_offset_ = geometry.indices.offset;
        draws_suppressed_ = false;
        return true;
    }

    bool VulkanCommandRecorder::BindResourceBindings(PipelineHandle pipeline,
                                                      DescriptorSetHandle bindings,
                                                      const DynamicUniformOffsets &dynamic_offsets)
    {
        ++profile_counters_.resource_binding_bind_requests;
        if (command_buffer_ == VK_NULL_HANDLE)
        {
            draws_suppressed_ = true;
            return false;
        }
        if (!pipeline_manager_ || !descriptor_set_manager_)
        {
            draws_suppressed_ = true;
            return false;
        }
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        const VkDescriptorSet descriptor_set = descriptor_set_manager_->GetDescriptorSet(bindings);
        if (pipeline_resource && descriptor_set != VK_NULL_HANDLE)
        {
            if (recorded_bindings_pipeline_ == pipeline && recorded_bindings_ == bindings &&
                recorded_dynamic_offsets_ == dynamic_offsets)
            {
                return true;
            }
            vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_resource->layout, 0, 1, &descriptor_set,
                                    static_cast<uint32_t>(dynamic_offsets.size()),
                                    dynamic_offsets.empty() ? nullptr : dynamic_offsets.data());
            recorded_bindings_pipeline_ = pipeline;
            recorded_bindings_ = bindings;
            recorded_dynamic_offsets_ = dynamic_offsets;
            ++profile_counters_.resource_binding_bind_emitted;
            return true;
        }
        draws_suppressed_ = true;
        return false;
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
