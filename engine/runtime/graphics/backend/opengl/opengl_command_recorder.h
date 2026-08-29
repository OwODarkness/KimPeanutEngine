#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_COMMAND_RECORDER_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_COMMAND_RECORDER_H

#include <memory>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>

#include "common/command_recorder.h"
#include "common/render_target.h"

namespace kpengine::graphics
{
    class MeshManager;
    class OpenglBindlessTextureTable;
    class OpenglDescriptorSet;
    class OpenglPipelineManager;

    struct OpenglMappedUniformBuffer
    {
        GLuint native = 0;
        std::vector<uint8_t> data;
    };

    // Valid only for the OpenGL backend's active frame. It borrows recording
    // dependencies; OpenglBackend remains the resource and frame owner.
    class OpenglCommandRecorder final : public CommandRecorder
    {
    public:
        struct Services
        {
            OpenglPipelineManager *pipeline_manager = nullptr;
            MeshManager *mesh_manager = nullptr;
            OpenglBindlessTextureTable *bindless_texture_table = nullptr;
            const std::vector<RenderTargetResource> *render_targets = nullptr;
            const std::vector<GLuint> *render_target_framebuffers = nullptr;
            const HandleSystem<RenderTargetHandle> *render_target_handles = nullptr;
            std::vector<std::unique_ptr<OpenglDescriptorSet>> *resource_binding_sets = nullptr;
            const HandleSystem<DescriptorSetHandle> *resource_binding_set_handles = nullptr;
            std::unordered_map<uint32_t, OpenglMappedUniformBuffer> *mapped_uniform_buffers = nullptr;
        };

        explicit OpenglCommandRecorder(Services services);

        bool BeginRenderTarget(RenderTargetHandle target) override;
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
        Services services_;
        PipelineHandle recorded_pipeline_;
        uint32_t recorded_index_count_ = 0;
        RenderTargetHandle active_render_target_;
        // Suppresses draws when the bound pipeline's attachment formats do not
        // match the active render target; recording stays pass-scoped instead of
        // submitting a pipeline-state mismatch to the driver.
        bool draws_suppressed_ = false;
    };
}

#endif
