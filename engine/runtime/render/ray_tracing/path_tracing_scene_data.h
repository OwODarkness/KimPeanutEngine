#ifndef KPENGINE_RUNTIME_RENDER_RAY_TRACING_PATH_TRACING_SCENE_DATA_H
#define KPENGINE_RUNTIME_RENDER_RAY_TRACING_PATH_TRACING_SCENE_DATA_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "math/math_header.h"

namespace kpengine::render::ray_tracing::path_trace_scene_data
{
    inline constexpr uint64_t kCameraUniformKey = 0x5254505443414d45ull;
    inline constexpr std::size_t kMaximumSceneRecords = 512;
    inline constexpr uint32_t kDirectLightSamples = 1;
    inline constexpr uint32_t kIntegratorVersion = 5;
    inline constexpr uint32_t kRngSeed = 0x52463436u;
    inline constexpr uint32_t kRngPolicyVersion = 3;
    inline constexpr float kRayMinimumDistance = 0.001f;
    inline constexpr float kRayMaximumDistance = 1000.0f;
    inline constexpr float kSecondaryRayOffset = 0.002f;

    struct alignas(16) PathTracingGeometryGpuData
    {
        std::array<uint32_t, 8> words{};
    };

    struct alignas(16) PathTracingInstanceGpuData
    {
        uint32_t geometry_offset = 0;
        uint32_t material_offset = 0;
        uint32_t geometry_count = 0;
        uint32_t padding = 0;
    };

    struct alignas(16) PathTracingMaterialGpuData
    {
        std::array<float, 4> base_color{0.72f, 0.72f, 0.72f, 1.0f};
        std::array<float, 4> emissive{};
        std::array<float, 4> surface{};
        std::array<uint32_t, 4> texture_indices{};
    };

    struct alignas(16) PathTracingLightGpuData
    {
        std::array<float, 4> position_or_type{};
        std::array<float, 4> direction_and_range{};
        std::array<float, 4> color_intensity{};
        std::array<float, 4> parameters{};
    };

    struct PathTracingSceneGpuData
    {
        std::array<PathTracingGeometryGpuData, kMaximumSceneRecords> geometry_data{};
        std::array<PathTracingInstanceGpuData, kMaximumSceneRecords> instance_data{};
        std::array<PathTracingMaterialGpuData, kMaximumSceneRecords> material_data{};
        std::array<PathTracingLightGpuData, 128> light_data{};
    };

    struct PathTracingCameraGpuData
    {
        Matrix4f inverse_view_projection;
        Vector4f camera_position;
        Vector4f light_center;
        Vector4f light_u;
        Vector4f light_v;
        Vector4f light_radiance;
        uint32_t rng_seed = kRngSeed;
        uint32_t sample_count = 0;
        uint32_t samples_per_dispatch = 1;
        uint32_t probe_mode = 0;
        uint32_t scene_data[4]{};
        uint32_t random_frame_index = 0;
        uint32_t padding[3]{};
    };

    static_assert(offsetof(PathTracingCameraGpuData, rng_seed) == 144);
    static_assert(offsetof(PathTracingCameraGpuData, scene_data) == 160);
    static_assert(sizeof(PathTracingGeometryGpuData) == 32);
    static_assert(sizeof(PathTracingInstanceGpuData) == 16);
    static_assert(sizeof(PathTracingMaterialGpuData) == 64);
    static_assert(sizeof(PathTracingLightGpuData) == 64);
    static_assert(offsetof(PathTracingCameraGpuData, random_frame_index) == 176);
    static_assert(sizeof(PathTracingCameraGpuData) == 192);
    static_assert(sizeof(PathTracingSceneGpuData) ==
                  kMaximumSceneRecords * (32 + 16 + 64) + 128 * 64);
}

#endif
