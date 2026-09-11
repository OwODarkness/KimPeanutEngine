#include "opengl_command_recorder.h"

#include <chrono>
#include <limits>
#include <string>

#include "common/mesh.h"
#include "common/mesh_manager.h"
#include "common/render_target_validation.h"
#include "log/logger.h"

#define KP_OPENGL_COMMAND_RECORDER_LOG_NAME "OpenglCommandRecorderLog"
#include "opengl_bindless_texture_table.h"
#include "opengl_descriptorset.h"
#include "opengl_mesh.h"
#include "opengl_pipeline.h"
#include "opengl_pipeline_manager.h"

namespace kpengine::graphics
{
    void OpenglCommandRecorder::ResetStateCache() noexcept
    {
        recorded_pipeline_ = {};
        recorded_mesh_ = {};
        recorded_bindings_ = {};
        recorded_bindings_pipeline_ = {};
        recorded_dynamic_offsets_.clear();
        validated_pipeline_ = {};
        validated_target_ = {};
        cached_pipeline_compatibility_ = false;
        recorded_index_count_ = 0;
        recorded_first_index_ = 0;
        recorded_index_offset_ = 0;
        recorded_index_type_ = IndexElementType::UInt32;
        recorded_geometry_ = false;
    }

    OpenglCommandRecorder::OpenglCommandRecorder(Services services)
        : services_(services)
    {
        profile_counters_ = {};
    }

    bool OpenglCommandRecorder::BeginRenderTarget(RenderTargetHandle target)
    {
        if (active_render_target_.IsValid() || presentation_active_ ||
            !services_.render_target_handles ||
            !services_.render_targets || !services_.render_target_framebuffers)
        {
            draws_suppressed_ = true;
            return false;
        }

        const uint32_t index = services_.render_target_handles->Get(target);
        if (index >= services_.render_targets->size() ||
            index >= services_.render_target_framebuffers->size())
        {
            draws_suppressed_ = true;
            return false;
        }

        const GLuint framebuffer = (*services_.render_target_framebuffers)[index];
        if (framebuffer == 0)
        {
            draws_suppressed_ = true;
            return false;
        }

        const RenderTargetResource &resource = (*services_.render_targets)[index];
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, static_cast<GLsizei>(resource.desc.width),
                   static_cast<GLsizei>(resource.desc.height));

        // One draw buffer per color attachment; depth-only targets draw nothing.
        const uint32_t color_count =
            static_cast<uint32_t>(resource.desc.color_attachments.size());
        if (color_count == 0)
        {
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }
        else
        {
            std::vector<GLenum> draw_buffers(color_count);
            for (uint32_t i = 0; i < color_count; ++i)
            {
                draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
            }
            glDrawBuffers(color_count, draw_buffers.data());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        for (uint32_t i = 0; i < color_count; ++i)
        {
            if (resource.desc.color_attachments[i].load_op == RenderTargetLoadOp::Clear)
            {
                glClearBufferfv(GL_COLOR, static_cast<GLint>(i),
                                resource.desc.color_attachments[i].clear_color.data());
            }
        }
        if (resource.desc.depth.has_value() &&
            resource.desc.depth->load_op == RenderTargetLoadOp::Clear)
        {
            // ClearBuffer obeys GL_DEPTH_WRITEMASK. A preceding color-only
            // pipeline leaves depth writes disabled, so make the attachment
            // load operation independent of stale draw-pipeline state. The
            // pipeline bound for this target will establish its draw state.
            glDepthMask(GL_TRUE);
            glClearBufferfv(GL_DEPTH, 0, &resource.desc.depth->clear_depth);
        }
        draws_suppressed_ = false;
        active_render_target_ = target;
        ResetStateCache();
        return true;
    }

    bool OpenglCommandRecorder::BeginPresentation(const std::array<float, 4> *clear_color)
    {
        if (active_render_target_.IsValid() || presentation_active_ ||
            services_.presentation_width <= 0 || services_.presentation_height <= 0)
        {
            draws_suppressed_ = true;
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, services_.presentation_width, services_.presentation_height);
        glScissor(0, 0, services_.presentation_width, services_.presentation_height);
        glDrawBuffer(GL_BACK);
        const std::array<float, 4> default_clear_color{0.015f, 0.015f, 0.02f, 1.0f};
        glClearBufferfv(GL_COLOR, 0,
                        (clear_color != nullptr ? clear_color : &default_clear_color)->data());
        presentation_active_ = true;
        draws_suppressed_ = false;
        ResetStateCache();
        return true;
    }

    void OpenglCommandRecorder::EndRenderTarget()
    {
        if (presentation_active_)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_FRAMEBUFFER_SRGB);
            presentation_active_ = false;
            draws_suppressed_ = false;
            ResetStateCache();
            return;
        }
        if (!active_render_target_.IsValid())
        {
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // OpenglPipeline enables this for the sRGB scene color attachment. The
        // default framebuffer belongs to the subsequent UI renderer instead.
        glDisable(GL_FRAMEBUFFER_SRGB);
        active_render_target_ = {};
        draws_suppressed_ = false;
        ResetStateCache();
    }

    bool OpenglCommandRecorder::BindPipeline(PipelineHandle pipeline)
    {
        ++profile_counters_.pipeline_bind_requests;
        if (!services_.pipeline_manager)
        {
            draws_suppressed_ = true;
            return false;
        }

        OpenglPipeline *resource = services_.pipeline_manager->GetPipelineResource(pipeline);
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
                                       validated_target_ == active_render_target_;
        if (validation_cached && !cached_pipeline_compatibility_)
        {
            draws_suppressed_ = true;
            return false;
        }

        bool compatible = true;
        if (!validation_cached && active_render_target_.IsValid() && services_.render_target_handles &&
            services_.render_targets)
        {
            const uint32_t index = services_.render_target_handles->Get(active_render_target_);
            if (index < services_.render_targets->size())
            {
                const auto validation_started = std::chrono::steady_clock::now();
                PipelineDesc pipeline_desc{};
                pipeline_desc.color_attachment_formats = resource->color_attachment_formats_;
                pipeline_desc.depth_attachment_format = resource->depth_attachment_format_;
                pipeline_desc.multisample_state.rasterization_samples =
                    resource->rasterization_samples_;
                std::string error;
                compatible = ValidateRenderTargetPipelineCompatibility(
                    (*services_.render_targets)[index].desc, pipeline_desc, &error);
                ++profile_counters_.pipeline_validation_calls;
                profile_counters_.pipeline_validation_cpu_ms +=
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - validation_started)
                        .count();
                if (!compatible)
                {
                    KP_LOG(KP_OPENGL_COMMAND_RECORDER_LOG_NAME, LOG_LEVEL_ERROR,
                           "Rejected pipeline for incompatible render target: %s",
                           error.c_str());
                    draws_suppressed_ = true;
                }
            }
        }

        validated_pipeline_ = pipeline;
        validated_target_ = active_render_target_;
        cached_pipeline_compatibility_ = compatible;
        if (!compatible)
        {
            draws_suppressed_ = true;
            ResetStateCache();
            return false;
        }
        draws_suppressed_ = false;

        resource->Bind();
        if (services_.bindless_texture_table)
        {
            services_.bindless_texture_table->Bind();
        }
        glBindVertexArray(resource->vao);
        recorded_pipeline_ = pipeline;
        recorded_mesh_ = {};
        recorded_bindings_ = {};
        recorded_bindings_pipeline_ = {};
        recorded_dynamic_offsets_.clear();
        ++profile_counters_.pipeline_bind_emitted;
        return true;
    }

    void OpenglCommandRecorder::BindMesh(MeshHandle mesh)
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
        if (!services_.mesh_manager || !services_.pipeline_manager)
        {
            reject();
            return;
        }
        if (recorded_mesh_ == mesh && recorded_pipeline_.IsValid() &&
            !recorded_geometry_ && !draws_suppressed_)
        {
            return;
        }

        Mesh *mesh_object = services_.mesh_manager->GetMesh(mesh);
        const auto *mesh_resource = mesh_object
                                        ? static_cast<const OpenglMeshResource *>(
                                              mesh_object->GetMeshHandle().native)
                                        : nullptr;
        OpenglPipeline *pipeline =
            services_.pipeline_manager->GetPipelineResource(recorded_pipeline_);
        if (!mesh_resource || !pipeline)
        {
            reject();
            return;
        }

        glVertexArrayVertexBuffer(pipeline->vao, 0, mesh_resource->vbo, 0, sizeof(Vertex));
        glVertexArrayElementBuffer(pipeline->vao, mesh_resource->ebo);
        glBindVertexArray(pipeline->vao);
        recorded_index_count_ = mesh_resource->sections.empty()
                                    ? 0u
                                    : static_cast<uint32_t>(mesh_resource->sections[0].index_count);
        recorded_first_index_ = mesh_resource->sections.empty()
                                     ? 0u
                                     : static_cast<uint32_t>(mesh_resource->sections[0].index_start);
        recorded_index_offset_ = 0;
        recorded_index_type_ = IndexElementType::UInt32;
        recorded_geometry_ = false;
        recorded_mesh_ = mesh;
        draws_suppressed_ = false;
        ++profile_counters_.mesh_bind_emitted;
    }

    bool OpenglCommandRecorder::BindGeometry(const GeometryView &geometry)
    {
        ++profile_counters_.mesh_bind_requests;
        OpenglPipeline *const pipeline = services_.pipeline_manager
                                             ? services_.pipeline_manager->GetPipelineResource(
                                                   recorded_pipeline_)
                                             : nullptr;
        if (!pipeline || !services_.get_geometry_buffer_desc ||
            !services_.get_geometry_buffer)
        {
            recorded_mesh_ = {};
            recorded_geometry_ = false;
            draws_suppressed_ = true;
            return false;
        }

        std::string error;
        if (!ValidateGeometryView(geometry, pipeline->binding_descs_,
                                  services_.get_geometry_buffer_desc, &error))
        {
            KP_LOG(KP_OPENGL_COMMAND_RECORDER_LOG_NAME, LOG_LEVEL_ERROR,
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
            const GLuint native = services_.get_geometry_buffer(view.buffer);
            if (native == 0)
            {
                recorded_mesh_ = {};
                recorded_geometry_ = false;
                draws_suppressed_ = true;
                return false;
            }
            const auto binding_it = std::find_if(
                pipeline->binding_descs_.begin(), pipeline->binding_descs_.end(),
                [&view](const VertexBindingDesc &binding) {
                    return binding.binding == view.binding;
                });
            if (binding_it == pipeline->binding_descs_.end())
            {
                recorded_mesh_ = {};
                recorded_geometry_ = false;
                draws_suppressed_ = true;
                return false;
            }
            glVertexArrayVertexBuffer(pipeline->vao, view.binding, native,
                                      static_cast<GLintptr>(view.offset),
                                      static_cast<GLsizei>(binding_it->stride));
        }

        const GLuint index_native = services_.get_geometry_buffer(geometry.indices.buffer);
        if (index_native == 0)
        {
            recorded_mesh_ = {};
            recorded_geometry_ = false;
            draws_suppressed_ = true;
            return false;
        }
        glVertexArrayElementBuffer(pipeline->vao, index_native);
        glBindVertexArray(pipeline->vao);
        recorded_mesh_ = {};
        recorded_geometry_ = true;
        recorded_index_type_ = geometry.indices.type;
        recorded_index_count_ = 0;
        recorded_first_index_ = 0;
        recorded_index_offset_ = geometry.indices.offset;
        draws_suppressed_ = false;
        return true;
    }

    bool OpenglCommandRecorder::BindResourceBindings(PipelineHandle pipeline,
                                                      DescriptorSetHandle bindings,
                                                      const DynamicUniformOffsets &dynamic_offsets)
    {
        ++profile_counters_.resource_binding_bind_requests;
        if (!services_.pipeline_manager ||
            !services_.pipeline_manager->GetPipelineResource(pipeline) ||
            !services_.resource_binding_set_handles || !services_.resource_binding_sets)
        {
            draws_suppressed_ = true;
            return false;
        }

        const uint32_t index = services_.resource_binding_set_handles->Get(bindings);
        if (index >= services_.resource_binding_sets->size() ||
            !(*services_.resource_binding_sets)[index])
        {
            draws_suppressed_ = true;
            return false;
        }

        const bool redundant = recorded_bindings_pipeline_ == pipeline &&
                               recorded_bindings_ == bindings &&
                               recorded_dynamic_offsets_ == dynamic_offsets;

        if (services_.flush_dirty_uniform_buffers)
        {
            services_.flush_dirty_uniform_buffers();
        }
        if (redundant)
        {
            return true;
        }
        (*services_.resource_binding_sets)[index]->Bind(dynamic_offsets);
        recorded_bindings_pipeline_ = pipeline;
        recorded_bindings_ = bindings;
        recorded_dynamic_offsets_ = dynamic_offsets;
        ++profile_counters_.resource_binding_bind_emitted;
        return true;
    }

    void OpenglCommandRecorder::SetViewport(const Viewport &viewport)
    {
        glViewport(static_cast<GLint>(viewport.x), static_cast<GLint>(viewport.y),
                   static_cast<GLsizei>(viewport.width), static_cast<GLsizei>(viewport.height));
    }

    void OpenglCommandRecorder::SetScissor(const Scissor &scissor)
    {
        glScissor(scissor.x, scissor.y, static_cast<GLsizei>(scissor.width),
                  static_cast<GLsizei>(scissor.height));
    }

    void OpenglCommandRecorder::DrawIndexed(uint32_t index_count, uint32_t instance_count,
                                             uint32_t first_index, int32_t vertex_offset,
                                             uint32_t first_instance)
    {
        (void)first_instance;
        if (draws_suppressed_) return;
        const OpenglPipeline *const pipeline = services_.pipeline_manager
                                                   ? services_.pipeline_manager->GetPipelineResource(
                                                         recorded_pipeline_)
                                                   : nullptr;
        if (!pipeline)
        {
            draws_suppressed_ = true;
            return;
        }
        const uint32_t count = index_count == 0 ? recorded_index_count_ : index_count;
        const uint32_t offset = index_count == 0 ? recorded_first_index_ : first_index;
        if (count != 0)
        {
            const GLenum index_type = recorded_index_type_ == IndexElementType::UInt16
                                          ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
            const size_t element_size = recorded_index_type_ == IndexElementType::UInt16
                                             ? sizeof(uint16_t) : sizeof(uint32_t);
            if (offset > (std::numeric_limits<size_t>::max() - recorded_index_offset_) /
                            element_size)
            {
                draws_suppressed_ = true;
                return;
            }
            const size_t byte_offset = recorded_index_offset_ + offset * element_size;
            glDrawElementsInstancedBaseVertex(
                pipeline->primitive_topology_type_, static_cast<GLsizei>(count), index_type,
                reinterpret_cast<const void *>(byte_offset),
                static_cast<GLsizei>(instance_count), vertex_offset);
            ++profile_counters_.draw_calls_emitted;
        }
    }
}
