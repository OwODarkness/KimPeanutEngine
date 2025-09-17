#ifndef KPENGINE_RUNTIME_GRAPHICS_BACKEND_ENUM_H
#define KPENGINE_RUNTIME_GRAPHICS_BACKEND_ENUM_H

#include <cstdint>
namespace kpengine::graphics
{
    enum class TextureFormat
    {
        TEXTURE_FORMAT_UNKOWN,
        TEXTURE_FORMAT_R8_UNORM,
        TEXTURE_FORMAT_RG8_UNORM,
        TEXTURE_FORMAT_RGB8_UNORM,
        TEXTURE_FORMAT_RGB8_SRGB,
        TEXTURE_FORMAT_RGBA8_UNORM,
        TEXTURE_FORMAT_RGBA8_SRGB,
        TEXTURE_FORMAT_D24S8,
        TEXTURE_FORMAT_D32
    };

    enum class TextureType
    {
        TEXTURE_TYPE_1D,
        TEXTURE_TYPE_2D,
        TEXTURE_TYPE_3D
    };

    enum class TextureUsage : uint32_t
    {
        None = 0,
        TEXTURE_USAGE_SAMPLE = 1 << 0,
        TEXTURE_USAGE_COLOR_ATTACHMENT = 1 << 1,
        TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT = 1 << 2,
        TEXTURE_USAGE_STORAGE = 1 << 3,
        TEXTURE_USAGE_TRANSFER_SRC = 1 << 4,
        TEXTURE_USAGE_TRANSFER_DST = 1 << 5,
    };

    inline TextureUsage operator|(TextureUsage lhs, TextureUsage rhs)
    {
        return static_cast<TextureUsage>(
            static_cast<uint32_t>(lhs) |
            static_cast<uint32_t>(rhs)
        );
    }

    enum class ShaderType
    {
        None,
        SHADER_TYPE_VERTEX,
        SHADER_TYPE_FRAGMENT,
        SHADER_TYPE_GEOMETRY
    };

    enum class VertexFormat
    {
        VERTEX_FORMAT_ONE_FLOAT,
        VERTEX_FORMAT_TWO_FLOATS,
        VERTEX_FORMAT_THREE_FLOATS,
        VERTEX_FORMAT_FOUR_FLOATS,
        VERTEX_FORMAT_ONE_INT,
        VERTEX_FORMAT_TWO_INTS,
        VERTEX_FORMAT_THREE_INTS,
        VERTEX_FORMAT_FOUR_INTS
    };

    enum class PrimitiveTopologyType
    {
        PRIMITIVE_TOPOLOGY_POINT_LIST,
        PRIMITIVE_TOPOLOGY_LINE_LIST,
        PRIMITIVE_TOPOLOGY_LINE_STRIP,
        PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        PRIMITIVE_TOPOLOGY_LINE_LIST_ADJACENCY,
        PRIMITIVE_TOPOLOGY_LINE_STRIP_ADJACENCY,
        PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_ADJACENCY,
        PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_ADJACENCY,
        PRIMITIVE_TOPOLOGY_PATCH_LIST
    };

    enum class CullMode
    {
        CULL_MODE_FRONT,
        CULL_MODE_BACK,
        CULL_MODE_FRONT_AND_BACK
    };

    enum class FrontFace
    {
        FRONT_FACE_COUNTER_CLOCKWISE,
        FRONT_FACE_CLOCKWISE
    };

    enum class PolygonMode
    {
        POLYGON_MODE_FILL,
        POLYGON_MODE_LINE,
        POLYGON_MODE_POINT
    };

    enum class BlendFactor
    {
        BLEND_FACTOR_ZERO,
        BLEND_FACTOR_ONE,
        BLEND_FACTOR_SRC_COLOR,
        BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        BLEND_FACTOR_DST_COLOR,
        BLEND_FACTOR_ONE_MINUS_DST_COLOR,
        BLEND_FACTOR_SRC_ALPHA,
        BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        BLEND_FACTOR_DST_ALPHA,
        BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        BLEND_FACTOR_CONSTANT_COLOR,
        BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
        BLEND_FACTOR_CONSTANT_ALPHA,
        BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA
    };

    enum class BlendOp
    {
        BLEND_OP_ADD,
        BLEND_OP_SUBTRACT,
        BLEND_OP_REVERSE_SUBTRACT,
        BLEND_OP_MIN,
        BLEND_OP_MAX
    };

    enum class DescriptorType
    {
        DESCRIPTOR_TYPE_UNIFORM,
        DESCRIPTOR_TYPE_UNIFORM_DYNAMIC,
        DESCRIPTOR_TYPE_SAMPLER
    };

    enum class ShaderStage : uint32_t
    {
        SHADER_STAGE_VERTEX = 0x01,
        SHADER_STAGE_FRAGMENT = 0x02,
        SHADER_STAGE_GEOMETRY = 0X04

    };

}

#endif