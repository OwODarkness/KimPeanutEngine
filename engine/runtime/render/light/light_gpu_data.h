#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_GPU_DATA_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_GPU_DATA_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

    // A ShadowHandle is not a sampled GPU resource. Until D4 resolves a
    // scheduled job to a current frame binding, every record is unshadowed.
    enum class LightGpuShadowKind : uint32_t
    {
        Unshadowed,
        Directional2D,
        Spot2D,
        PointCube,
    };

    struct alignas(16) LightGpuData
    {
        std::array<float, 4> color_intensity{};
        std::array<float, 4> position_range{};
        std::array<float, 4> direction_inner_cone{};
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

    static_assert(sizeof(LightGpuData) % 16 == 0,
                  "LightGpuData rows must remain uniform-buffer aligned");
    static_assert(sizeof(LightGpuFrameHeader) == 16,
                  "The frame lighting header is one uniform-buffer row");

    bool IsLightGpuFrameHeaderCompatible(const LightGpuFrameHeader &header);
    std::optional<LightGpuData> EncodeLightGpuData(const LightDesc &light);
    LightGpuFrameData BuildLightGpuFrameData(const std::vector<Light> &lights);
}

#endif
