#ifndef KPENGINE_RUNTIME_GRAPHICS_COMMAND_RECORDER_H
#define KPENGINE_RUNTIME_GRAPHICS_COMMAND_RECORDER_H

#include <array>
#include <cstdint>

#include "api.h"
#include "buffer_types.h"
#include "profile_counters.h"
#include "resource_binding.h"

namespace kpengine::graphics
{
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float min_depth = 0.0f;
        float max_depth = 1.0f;
    };

    struct Scissor
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // Commands are valid only while the owning RenderBackend is between
    // BeginFrame() and EndFrame(). Implementations translate these intents to
    // the native graphics API command stream.
    class CommandRecorder
    {
    public:
        virtual ~CommandRecorder() = default;
        // Begins an offscreen render pass. The target must be ended before
        // beginning another target or ending the frame.
        virtual bool BeginRenderTarget(RenderTargetHandle target) = 0;
        // A null color preserves the backend's presentation default. A
        // supplied color is authored in display-space and is used for the
        // presentation attachment clear.
        virtual bool BeginPresentation(const std::array<float, 4> *clear_color = nullptr) = 0;
        virtual void EndRenderTarget() = 0;
        virtual bool BindPipeline(PipelineHandle pipeline) = 0;
        virtual void BindMesh(MeshHandle mesh) = 0;
        virtual bool BindGeometry(const GeometryView &geometry) = 0;
        virtual bool BindResourceBindings(PipelineHandle pipeline,
                                          DescriptorSetHandle bindings,
                                          const DynamicUniformOffsets &dynamic_offsets = {}) = 0;
        virtual void SetViewport(const Viewport &viewport) = 0;
        virtual void SetScissor(const Scissor &scissor) = 0;
        virtual void DrawIndexed(uint32_t index_count = 0,
                                 uint32_t instance_count = 1,
                                 // Offset in index elements within the bound
                                 // mesh. A zero index_count uses the first
                                 // section's recorded range.
                                 uint32_t first_index = 0,
                                 int32_t vertex_offset = 0,
                                 uint32_t first_instance = 0) = 0;

        virtual CommandRecorderProfileCounters GetProfileCounters() const { return {}; }
    };
}

#endif
