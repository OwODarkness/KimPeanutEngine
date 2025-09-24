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
    struct BufferTag{};
    struct MeshTag{};

    using TextureHandle = Handle<TextureTag>;
    using SamplerHandle = Handle<SamplerTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using PipelineHandle = Handle<PipelineTag>;
    using BufferHandle = Handle<BufferTag>;
    using MeshHandle = Handle<MeshTag>;

}

#endif