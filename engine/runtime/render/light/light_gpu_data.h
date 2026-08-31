#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_GPU_DATA_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_GPU_DATA_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include "render/light/light_world.h"

namespace kpengine::render
{
    // This is a shader-facing Render ABI, intentionally separate from the
    // authored LightDesc. It uses 16-byte rows so the same payload is legal for
    // the current Vulkan and OpenGL uniform-buffer paths.
    constexpr uint32_t kLightGpuDataAbiVersion = 1;
    constexpr uint32_t kMaxFrameLights = 64;
    constexpr uint32_t kFrameLightingDescriptorSet = 0;
    constexpr uint32_t kFrameLightingDescriptorBinding = 4;

    enum class LightGpuType : uint32_t
    {
        Directional,
        Point,
        Spot,
    };

    // A ShadowHandle is not a sampled GPU resource. Only a matching scheduled
    // frame binding may promote an authored shadow to a shader-visible kind.
    enum class LightGpuShadowKind : uint32_t
    {
        Unshadowed,
        Directional2D,
        Spot2D,
        PointCube,
    };

    struct alignas(16) LightGpuData
    {
        Vector4f color_intensity{};
        Vector4f position_range{};
        Vector4f direction_inner_cone{};
        float outer_cone_radians = 0.0f;
        uint32_t type = static_cast<uint32_t>(LightGpuType::Directional);
        uint32_t shadow_kind = static_cast<uint32_t>(LightGpuShadowKind::Unshadowed);
        uint32_t shadow_binding_slot = 0;
        uint32_t layer_mask = 0;
        uint32_t enabled = 0;
        std::array<uint32_t, 3> reserved{};
    };

    struct alignas(16) LightGpuFrameHeader
    {
        uint32_t abi_version = kLightGpuDataAbiVersion;
        uint32_t light_count = 0;
        uint32_t light_stride = sizeof(LightGpuData);
        uint32_t reserved = 0;
    };

    struct alignas(16) LightGpuFrameData
    {
        LightGpuFrameHeader header{};
        std::array<LightGpuData, kMaxFrameLights> lights{};
    };

    // Frame-local resolution of an authored shadow identity to a scheduled GPU
    // binding. The sampled texture remains owned by RendererFrameTargets.
    struct ResolvedLightShadowBinding
    {
        LightHandle source_light;
        ShadowHandle shadow;
        ShadowKind kind = ShadowKind::Directional2D;
        uint32_t binding_slot = 0;
    };

    // Fullscreen lighting constants. The matrix is transposed before upload so
    // GLSL observes the CPU-side inverse view-projection matrix.
    struct alignas(16) DeferredLightingGpuData
    {
        Matrix4f inverse_view_projection;
        Vector4f camera_world_position{};
        Matrix4f directional_shadow_view_projection;
        // Minimum bias, slope-scaled bias, texel size, reserved.
        Vector4f directional_shadow_params{};
        // IBL enabled, prefilter atlas level count, intensity, reserved.
        Vector4f environment_ibl_params{};
    };

    static_assert(sizeof(Vector4f) == sizeof(float) * 4,
                  "Vector4f must remain a four-float shader ABI row");
    static_assert(std::is_trivially_copyable_v<Vector4f>,
                  "Vector4f must be safe to copy into uniform buffers");
    static_assert(sizeof(LightGpuData) == 96,
                  "LightGpuData must match the version-1 GLSL std140 stride");
    static_assert(offsetof(LightGpuData, outer_cone_radians) == 48);
    static_assert(offsetof(LightGpuData, layer_mask) == 64);
    static_assert(offsetof(LightGpuData, reserved) == 72);
    static_assert(sizeof(LightGpuFrameHeader) == 16,
                  "The frame lighting header is one uniform-buffer row");
    static_assert(sizeof(DeferredLightingGpuData) == 176,
                  "Deferred lighting constants must match eleven std140 rows");

    bool IsLightGpuFrameHeaderCompatible(const LightGpuFrameHeader &header);
    std::optional<LightGpuData> EncodeLightGpuData(const LightDesc &light);
    LightGpuFrameData BuildLightGpuFrameData(
        const std::vector<Light> &lights,
        const std::optional<ResolvedLightShadowBinding> &resolved_shadow = std::nullopt);
}

#endif
