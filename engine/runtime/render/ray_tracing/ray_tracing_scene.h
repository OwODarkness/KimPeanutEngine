#ifndef KPENGINE_RUNTIME_RENDER_RAY_TRACING_RAY_TRACING_SCENE_H
#define KPENGINE_RUNTIME_RENDER_RAY_TRACING_RAY_TRACING_SCENE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "graphics/backend/common/ray_tracing.h"
#include "render/light/light_world.h"
#include "render/ray_tracing/ray_tracing_scene_view.h"
#include "render/ray_tracing_scene_sections.h"
#include "render/ray_tracing_scene_signature.h"

namespace kpengine::graphics
{
    class RenderBackend;
    class RayTracingResourceOwner;
}

namespace kpengine::render
{
    using ray_tracing::RayTracingSceneView;

    class MaterialSystem;
    class RenderGraphPassContext;
    class RenderResourceResolver;
    class RenderWorld;
    class SceneDrawRecorder;

    struct RayTracingScenePrepareContext
    {
        graphics::RenderBackend &backend;
        const RenderWorld *world = nullptr;
        const SceneDrawRecorder &draw_recorder;
        MaterialSystem *materials = nullptr;
        RenderResourceResolver *resources = nullptr;
        bool path_tracing_enabled = false;
    };

    struct RayTracingSceneTableUpdate
    {
        bool succeeded = false;
        graphics::RayTracingBufferReferenceTableHandle table;
        std::size_t records_packed = 0;
        std::size_t records_uploaded = 0;
        double cpu_pack_ms = 0.0;
    };

    class RayTracingScene final
    {
    public:
        bool Prepare(const RayTracingScenePrepareContext &context);
        void PrepareLights(std::span<const Light> lights, bool path_tracing_enabled);
        bool PrepareBuildResources(graphics::RayTracingResourceOwner &owner,
                                   uint64_t frame_number);
        void CancelPreparedBuildResources(graphics::RayTracingResourceOwner *owner) noexcept;
        bool RecordBlasBuild(const RenderGraphPassContext &context);
        bool RecordTlasBuild(const RenderGraphPassContext &context);
        RayTracingSceneTableUpdate EnsureReferenceTable(
            graphics::RayTracingResourceOwner &owner,
            std::span<const graphics::BufferHandle> declared_geometry);
        void Cleanup(graphics::RenderBackend *backend);
        RayTracingSceneView View() const noexcept;
        const std::optional<graphics::RayTracingBuildResources> &BlasResources() const noexcept;
        const std::optional<graphics::RayTracingBuildResources> &TlasResources() const noexcept;

    private:
        void MarkReferenceTableUpdated() noexcept;
        struct BlasState
        {
            graphics::AccelerationStructureHandle handle;
            uint32_t geometry_count = 0;
            uint64_t geometry_signature = 0;
            bool built = false;
        };

        struct MeshBuild
        {
            detail::RayTracingSectionKey key;
            graphics::AccelerationStructureHandle blas;
            std::size_t geometry_offset = 0;
            std::size_t geometry_count = 0;
            uint64_t geometry_signature = 0;
            bool needs_build = false;
        };

        void RebuildFrameViews();

        std::unordered_map<detail::RayTracingSectionKey, BlasState,
                           detail::RayTracingSectionKeyHash> ray_tracing_blas_;
        graphics::AccelerationStructureHandle ray_tracing_tlas_;
        uint32_t ray_tracing_tlas_capacity_ = 0;
        bool ray_tracing_tlas_built_ = false;
        uint64_t ray_tracing_instance_signature_ = 0;
        uint64_t frame_ray_tracing_instance_signature_ = 0;
        uint64_t frame_ray_tracing_material_signature_ = 0;
        std::vector<graphics::RayTracingGeometryDesc> frame_ray_tracing_geometries_;
        std::vector<graphics::RayTracingInstanceDesc> frame_ray_tracing_instances_;
        std::vector<ray_tracing::PathTracingInstanceRecord> frame_ray_tracing_instance_data_;
        std::vector<ray_tracing::PathTracingMaterialRecord> frame_ray_tracing_material_data_;
        std::vector<ray_tracing::PathTracingLightRecord> frame_ray_tracing_light_data_;
        std::vector<graphics::RayTracingBufferAddressPatch>
            frame_ray_tracing_scene_address_patches_;
        graphics::RayTracingBufferReferenceTableHandle ray_tracing_scene_table_;
        uint64_t ray_tracing_scene_cache_world_revision_ = 0;
        uint64_t ray_tracing_scene_cache_material_revision_ = 0;
        uint64_t ray_tracing_scene_cache_instance_signature_ = 0;
        uint64_t ray_tracing_scene_cache_material_signature_ = 0;
        uint64_t ray_tracing_scene_table_lighting_signature_ = 0;
        bool ray_tracing_scene_cache_path_tracing_enabled_ = false;
        bool ray_tracing_scene_cache_valid_ = false;
        bool frame_ray_tracing_scene_table_dirty_ = true;
        uint64_t ray_tracing_scene_record_cache_hits_total_ = 0;
        uint64_t ray_tracing_scene_record_cache_misses_total_ = 0;
        uint64_t frame_ray_tracing_lighting_signature_ = 0;
        std::vector<MeshBuild> frame_ray_tracing_mesh_builds_;
        std::vector<graphics::RayTracingBuildDesc> frame_ray_tracing_blas_builds_;
        std::vector<graphics::RayTracingBuildDesc> frame_ray_tracing_tlas_builds_;
        std::optional<graphics::RayTracingBuildResources> frame_ray_tracing_blas_resources_;
        std::optional<graphics::RayTracingBuildResources> frame_ray_tracing_tlas_resources_;
        std::vector<graphics::AccelerationStructureHandle> frame_ray_tracing_blas_handles_;
        std::vector<graphics::BufferHandle> frame_ray_tracing_instance_inputs_;
        std::vector<graphics::BufferHandle> frame_ray_tracing_scratch_buffers_;
        bool frame_ray_tracing_blas_build_ = false;
        bool frame_ray_tracing_tlas_build_ = false;
    };
}

#endif
