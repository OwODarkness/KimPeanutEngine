#include "opengl_command_recorder.h"

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
    OpenglCommandRecorder::OpenglCommandRecorder(Services services)
        : services_(services)
    {
    }

    bool OpenglCommandRecorder::BeginRenderTarget(RenderTargetHandle target)
    {
        if (active_render_target_.IsValid() || !services_.render_target_handles ||
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
            glClearBufferfv(GL_DEPTH, 0, &resource.desc.depth->clear_depth);
        }
        draws_suppressed_ = false;
        active_render_target_ = target;
        return true;
    }

    void OpenglCommandRecorder::EndRenderTarget()
    {
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
    }

    void OpenglCommandRecorder::BindPipeline(PipelineHandle pipeline)
    {
        if (!services_.pipeline_manager)
        {
            return;
        }

        OpenglPipeline *resource = services_.pipeline_manager->GetPipelineResource(pipeline);
        if (!resource)
        {
            draws_suppressed_ = true;
            return;
        }
        if (active_render_target_.IsValid() && services_.render_target_handles &&
            services_.render_targets)
        {
            const uint32_t index = services_.render_target_handles->Get(active_render_target_);
            if (index < services_.render_targets->size())
            {
                PipelineDesc pipeline_desc{};
                pipeline_desc.color_attachment_formats = resource->color_attachment_formats_;
                pipeline_desc.depth_attachment_format = resource->depth_attachment_format_;
                pipeline_desc.multisample_state.rasterization_samples =
                    resource->rasterization_samples_;
                std::string error;
                if (!ValidateRenderTargetPipelineCompatibility(
                        (*services_.render_targets)[index].desc, pipeline_desc, &error))
                {
                    KP_LOG(KP_OPENGL_COMMAND_RECORDER_LOG_NAME, LOG_LEVEL_ERROR,
                           "Rejected pipeline for incompatible render target: %s",
                           error.c_str());
                    draws_suppressed_ = true;
                    return;
                }
            }
        }

        resource->Bind();
        if (services_.bindless_texture_table)
        {
            services_.bindless_texture_table->Bind();
        }
        glBindVertexArray(resource->vao);
        recorded_pipeline_ = pipeline;
    }

    void OpenglCommandRecorder::BindMesh(MeshHandle mesh)
    {
        if (!services_.mesh_manager || !services_.pipeline_manager)
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
            return;
        }

        glVertexArrayVertexBuffer(pipeline->vao, 0, mesh_resource->vbo, 0, sizeof(Vertex));
        glVertexArrayElementBuffer(pipeline->vao, mesh_resource->ebo);
        glBindVertexArray(pipeline->vao);
        recorded_index_count_ = mesh_resource->sections.empty()
                                    ? 0u
                                    : static_cast<uint32_t>(mesh_resource->sections[0].index_count);
    }

    void OpenglCommandRecorder::BindResourceBindings(PipelineHandle pipeline,
                                                       DescriptorSetHandle bindings)
    {
        (void)pipeline;
        if (!services_.resource_binding_set_handles || !services_.resource_binding_sets ||
            !services_.mapped_uniform_buffers)
        {
            return;
        }

        const uint32_t index = services_.resource_binding_set_handles->Get(bindings);
        if (index >= services_.resource_binding_sets->size() ||
            !(*services_.resource_binding_sets)[index])
        {
            return;
        }

        for (const auto &[id, mapped] : *services_.mapped_uniform_buffers)
        {
            (void)id;
            glBindBuffer(GL_UNIFORM_BUFFER, mapped.native);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, mapped.data.size(), mapped.data.data());
        }
        (*services_.resource_binding_sets)[index]->Bind();
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
        (void)vertex_offset;
        (void)first_instance;
        if (draws_suppressed_) return;
        const uint32_t count = index_count == 0 ? recorded_index_count_ : index_count;
        if (count != 0)
        {
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(count),
                                    GL_UNSIGNED_INT,
                                    reinterpret_cast<const void *>(first_index * sizeof(uint32_t)),
                                    static_cast<GLsizei>(instance_count));
        }
    }
}
