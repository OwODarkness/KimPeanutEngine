#ifndef KPENGINE_RUNTIME_RENDER_RAY_TRACING_RAY_TRACING_SCENE_VIEW_H
#define KPENGINE_RUNTIME_RENDER_RAY_TRACING_RAY_TRACING_SCENE_VIEW_H

#include <cstdint>
#include <span>

#include "graphics/backend/common/ray_tracing.h"
#include "math/math_header.h"

namespace kpengine::render::ray_tracing
{
    struct PathTracingInstanceRecord
    {
        uint32_t geometry_offset = 0;
        uint32_t material_offset = 0;
        uint32_t geometry_count = 0;
    };

    struct PathTracingMaterialRecord
    {
        Vector4f base_color{0.72f, 0.72f, 0.72f, 1.0f};
        Vector4f emissive{0.0f, 0.0f, 0.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 1.0f;
        float normal_scale = 1.0f;
        uint32_t base_color_texture_index = 0xffffffffu;
        uint32_t metallic_texture_index = 0xffffffffu;
        uint32_t roughness_texture_index = 0xffffffffu;
        uint32_t metallic_channel = 0;
        uint32_t roughness_channel = 0;
    };

    struct PathTracingLightRecord
    {
        Vector4f position_or_type{};
        Vector4f direction_and_range{};
        Vector4f color_intensity{};
        Vector4f parameters{};
    };

    struct RayTracingSceneView
    {
        std::span<const graphics::RayTracingGeometryDesc> geometries;
        std::span<const graphics::RayTracingInstanceDesc> instances;
        std::span<const PathTracingInstanceRecord> path_instances;
        std::span<const PathTracingMaterialRecord> path_materials;
        std::span<const PathTracingLightRecord> path_lights;
        std::span<const graphics::AccelerationStructureHandle> bottom_levels;
        std::span<const graphics::RayTracingBuildDesc> bottom_level_builds;
        std::span<const graphics::RayTracingBuildDesc> top_level_builds;
        std::span<const graphics::BufferHandle> instance_inputs;
        std::span<const graphics::BufferHandle> scratch_buffers;
        graphics::AccelerationStructureHandle top_level;
        graphics::RayTracingBufferReferenceTableHandle reference_table;
        uint64_t instance_signature = 0;
        uint64_t material_signature = 0;
        uint64_t lighting_signature = 0;
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;
        bool reference_table_dirty = true;
        bool blas_build_required = false;
        bool tlas_build_required = false;
    };
}

#endif
