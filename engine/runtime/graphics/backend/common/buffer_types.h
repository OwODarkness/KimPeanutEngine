#ifndef KPENGINE_RUNTIME_GRAPHICS_BUFFER_TYPES_H
#define KPENGINE_RUNTIME_GRAPHICS_BUFFER_TYPES_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "api.h"
#include "enum.h"

namespace kpengine::graphics{
    enum class BufferRole
    {
        Vertex,
        Index
    };

    enum class BufferUpdateMode
    {
        Immutable,
        PerFrame
    };

    enum class IndexElementType
    {
        UInt16,
        UInt32
    };

    struct BufferDesc
    {
        BufferRole role = BufferRole::Vertex;
        BufferUpdateMode update_mode = BufferUpdateMode::Immutable;
        std::size_t capacity_bytes = 0;
    };

    struct VertexBufferView
    {
        uint32_t binding = 0;
        BufferHandle buffer;
        std::size_t offset = 0;
    };

    struct IndexBufferView
    {
        BufferHandle buffer;
        std::size_t offset = 0;
        IndexElementType type = IndexElementType::UInt32;
    };

    struct GeometryView
    {
        std::vector<VertexBufferView> vertices;
        IndexBufferView indices;
    };

    struct VertexBindingDesc
    {
        uint32_t binding;
        uint32_t stride;
        bool per_instance;
    };

    struct VertexAttributionDesc
    {
        uint32_t location;
        uint32_t binding;
        VertexFormat format;
        uint32_t offset;
    };

    using BufferDescLookup = std::function<std::optional<BufferDesc>(BufferHandle)>;

    bool ValidateBufferDesc(const BufferDesc &desc,
                            const void *initial_data,
                            std::size_t initial_size,
                            std::string *error = nullptr);

    bool ValidateGeometryView(const GeometryView &geometry,
                              const std::vector<VertexBindingDesc> &pipeline_bindings,
                              const BufferDescLookup &lookup,
                              std::string *error = nullptr);

}

#endif
