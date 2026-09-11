#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_COMMAND_RECORDER_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_COMMAND_RECORDER_H

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
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
        size_t dirty_begin = 0;
        size_t dirty_end = 0;

        bool HasDirtyRange() const noexcept { return dirty_begin < dirty_end; }

        void MarkDirtyRange(size_t offset, size_t size) noexcept
        {
            if (size == 0 || offset >= data.size())
            {
                return;
            }
            const size_t end = offset + std::min(size, data.size() - offset);
            if (!HasDirtyRange())
            {
                dirty_begin = offset;
                dirty_end = end;
                return;
            }
            dirty_begin = std::min(dirty_begin, offset);
            dirty_end = std::max(dirty_end, end);
        }

        void ClearDirtyRange() noexcept
        {
            dirty_begin = 0;
            dirty_end = 0;
        }
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
            std::function<std::optional<BufferDesc>(BufferHandle)> get_geometry_buffer_desc;
            std::function<GLuint(BufferHandle)> get_geometry_buffer;
            std::function<void()> flush_dirty_uniform_buffers;
            int presentation_width = 0;
            int presentation_height = 0;
        };

        explicit OpenglCommandRecorder(Services services);

        bool BeginRenderTarget(RenderTargetHandle target) override;
        bool BeginPresentation() override;
        void EndRenderTarget() override;
        bool BindPipeline(PipelineHandle pipeline) override;
        void BindMesh(MeshHandle mesh) override;
        bool BindGeometry(const GeometryView &geometry) override;
        bool BindResourceBindings(PipelineHandle pipeline,
                                  DescriptorSetHandle bindings,
                                  const DynamicUniformOffsets &dynamic_offsets = {}) override;
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

        Services services_;
        PipelineHandle recorded_pipeline_;
        uint32_t recorded_index_count_ = 0;
        uint32_t recorded_first_index_ = 0;
        size_t recorded_index_offset_ = 0;
        IndexElementType recorded_index_type_ = IndexElementType::UInt32;
        MeshHandle recorded_mesh_;
        bool recorded_geometry_ = false;
        DescriptorSetHandle recorded_bindings_;
        PipelineHandle recorded_bindings_pipeline_;
        DynamicUniformOffsets recorded_dynamic_offsets_;
        RenderTargetHandle validated_target_;
        PipelineHandle validated_pipeline_;
        bool cached_pipeline_compatibility_ = false;
        RenderTargetHandle active_render_target_;
        // Suppresses draws when the bound pipeline's attachment formats do not
        // match the active render target; recording stays pass-scoped instead of
        // submitting a pipeline-state mismatch to the driver.
        bool draws_suppressed_ = false;
        bool presentation_active_ = false;
        CommandRecorderProfileCounters profile_counters_{};
    };
}

#endif
