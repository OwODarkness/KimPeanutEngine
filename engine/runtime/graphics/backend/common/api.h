#ifndef KPENGINE_RUNTIME_GRAPHICS_API_H
#define KPENGINE_RUNTIME_GRAPHICS_API_H

#include <cstdint>
#include "base/handle.h"
namespace kpengine::graphics
{
    struct TextureTag{};
    struct SamplerTag{};
    struct ShaderTag{};
    struct PipelineTag{};
    struct DescriptorSetTag{};
    struct BufferTag{};
    struct MeshTag{};
    struct RenderTargetTag{};
    struct AudioTag{};
    struct AccelerationStructureTag{};
    struct RayTracingPipelineTag{};


    using TextureHandle = Handle<TextureTag>;
    using SamplerHandle = Handle<SamplerTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using PipelineHandle = Handle<PipelineTag>;
    using DescriptorSetHandle = Handle<DescriptorSetTag>;
    using BufferHandle = Handle<BufferTag>;
    using MeshHandle = Handle<MeshTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
    // Opaque Graphics-owned handles. Native acceleration structures and RT
    // pipelines never cross this common boundary.
    using AccelerationStructureHandle = Handle<AccelerationStructureTag>;
    using RayTracingPipelineHandle = Handle<RayTracingPipelineTag>;

}

#endif
