#ifndef KPENGINE_RUNTIME_GRAPHICS_COMMAND_RECORDER_H
#define KPENGINE_RUNTIME_GRAPHICS_COMMAND_RECORDER_H

#include <cstdint>

#include "api.h"

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
        virtual void BindPipeline(PipelineHandle pipeline) = 0;
        virtual void BindMesh(MeshHandle mesh) = 0;
        virtual void BindResourceBindings(PipelineHandle pipeline,
                                           DescriptorSetHandle bindings) = 0;
        virtual void SetViewport(const Viewport &viewport) = 0;
        virtual void SetScissor(const Scissor &scissor) = 0;
        virtual void DrawIndexed(uint32_t index_count = 0,
                                 uint32_t instance_count = 1,
                                 uint32_t first_index = 0,
                                 int32_t vertex_offset = 0,
                                 uint32_t first_instance = 0) = 0;
    };
}

#endif
