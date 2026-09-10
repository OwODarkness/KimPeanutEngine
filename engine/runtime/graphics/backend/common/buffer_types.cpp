#include "buffer_types.h"

#include <limits>
#include <unordered_set>

namespace kpengine::graphics
{
    namespace
    {
        void SetError(std::string *error, const char *message)
        {
            if (error)
            {
                *error = message;
            }
        }

        bool IsValidBufferRole(const BufferRole role) noexcept
        {
            return role == BufferRole::Vertex || role == BufferRole::Index;
        }

        bool IsValidUpdateMode(const BufferUpdateMode mode) noexcept
        {
            return mode == BufferUpdateMode::Immutable || mode == BufferUpdateMode::PerFrame;
        }

        bool IsValidIndexType(const IndexElementType type) noexcept
        {
            return type == IndexElementType::UInt16 || type == IndexElementType::UInt32;
        }

        std::size_t IndexElementSize(const IndexElementType type) noexcept
        {
            return type == IndexElementType::UInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
        }
    }

    bool ValidateBufferDesc(const BufferDesc &desc, const void *initial_data,
                            const std::size_t initial_size, std::string *error)
    {
        if (!IsValidBufferRole(desc.role))
        {
            SetError(error, "buffer role is invalid");
            return false;
        }
        if (!IsValidUpdateMode(desc.update_mode))
        {
            SetError(error, "buffer update mode is invalid");
            return false;
        }
        if (desc.capacity_bytes == 0)
        {
            SetError(error, "buffer capacity must be non-zero");
            return false;
        }
        if (initial_size != 0 && initial_data == nullptr)
        {
            SetError(error, "non-zero initial size requires initial data");
            return false;
        }
        if (initial_size > desc.capacity_bytes)
        {
            SetError(error, "initial data exceeds buffer capacity");
            return false;
        }
        if (desc.update_mode == BufferUpdateMode::PerFrame &&
            (initial_data != nullptr || initial_size != 0))
        {
            SetError(error, "per-frame buffers cannot have initial data");
            return false;
        }
        return true;
    }

    bool ValidateGeometryView(const GeometryView &geometry,
                              const std::vector<VertexBindingDesc> &pipeline_bindings,
                              const BufferDescLookup &lookup,
                              std::string *error)
    {
        if (!lookup || pipeline_bindings.empty() || geometry.vertices.empty())
        {
            SetError(error, "geometry view has no vertex streams or lookup");
            return false;
        }
        if (!geometry.indices.buffer.IsValid())
        {
            SetError(error, "geometry view has an invalid index buffer");
            return false;
        }
        if (!IsValidIndexType(geometry.indices.type))
        {
            SetError(error, "geometry view index type is invalid");
            return false;
        }

        std::unordered_set<uint32_t> pipeline_binding_ids;
        for (const VertexBindingDesc &binding : pipeline_bindings)
        {
            if (!pipeline_binding_ids.insert(binding.binding).second)
            {
                SetError(error, "pipeline contains duplicate vertex bindings");
                return false;
            }
        }
        if (geometry.vertices.size() != pipeline_binding_ids.size())
        {
            SetError(error, "geometry view does not provide exactly one stream per pipeline binding");
            return false;
        }

        std::unordered_set<uint32_t> geometry_binding_ids;
        for (const VertexBufferView &view : geometry.vertices)
        {
            if (!view.buffer.IsValid() || !geometry_binding_ids.insert(view.binding).second)
            {
                SetError(error, "geometry view contains an invalid or duplicate vertex binding");
                return false;
            }
            if (pipeline_binding_ids.find(view.binding) == pipeline_binding_ids.end())
            {
                SetError(error, "geometry view contains a binding not declared by the pipeline");
                return false;
            }
            const std::optional<BufferDesc> desc = lookup(view.buffer);
            if (!desc.has_value() || desc->role != BufferRole::Vertex ||
                view.offset > desc->capacity_bytes)
            {
                SetError(error, "vertex stream handle, role, or offset is invalid");
                return false;
            }
        }

        if (geometry_binding_ids.size() != pipeline_binding_ids.size())
        {
            SetError(error, "geometry view is missing a pipeline vertex binding");
            return false;
        }

        const std::optional<BufferDesc> index_desc = lookup(geometry.indices.buffer);
        const std::size_t element_size = IndexElementSize(geometry.indices.type);
        if (!index_desc.has_value() || index_desc->role != BufferRole::Index ||
            geometry.indices.offset > index_desc->capacity_bytes ||
            geometry.indices.offset % element_size != 0)
        {
            SetError(error, "index stream handle, role, or offset is invalid");
            return false;
        }
        return true;
    }
}
