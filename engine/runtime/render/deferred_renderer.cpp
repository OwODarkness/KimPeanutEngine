#include "deferred_renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/command_recorder.h"
#include "log/logger.h"
#include "render/camera_utils.h"
#include "render/path_trace_history_signature.h"
#include "render/path_trace_history_progress.h"
#include "render/ray_tracing_scene_signature.h"
#include "render/material/material_system.h"
#include "render/material/material_asset_resolver.h"
#include "render/render_capture_service_internal.h"
#include "render/render_world/scene_draw_list.h"
#include "render/render_world/scene_visibility.h"
#include "render_resource_resolver.h"
#include "render_scene_coordinator.h"

namespace kpengine::render
{
    static_assert(static_cast<uint8_t>(RenderProfilePass::Count) ==
                  static_cast<uint8_t>(FixedRenderPassId::Count));

    namespace
    {
        constexpr const char *GetGraphicsApiName(GraphicsAPIType api_type)
        {
            switch (api_type)
            {
            case GraphicsAPIType::GRAPHICS_API_OPENGL:
                return "OpenGL";
            case GraphicsAPIType::GRAPHICS_API_VULKAN:
                return "Vulkan";
            case GraphicsAPIType::GRAPHICS_API_UNKNOW:
            default:
                return "Unknown";
            }
        }

        constexpr uint32_t kPointShadowFaceResolution = 512;
        constexpr uint32_t kPointShadowAtlasWidth = kPointShadowFaceResolution * 3;
        constexpr uint32_t kPointShadowAtlasHeight = kPointShadowFaceResolution * 2;
        constexpr uint64_t kPointShadowTargetBytes =
            static_cast<uint64_t>(kPointShadowAtlasWidth) * kPointShadowAtlasHeight * 4;
        constexpr uint64_t kDirectionalShadowPerPassUniformKey = 0x534841444f575f44ull;
        constexpr uint64_t kSpotShadowPerPassUniformKey = 0x534841444f575f53ull;
        constexpr uint64_t kPointShadowPerPassUniformKey = 0x534841444f575f50ull;
        constexpr uint64_t kGBufferPerPassUniformKey = 0x4742554646455250ull;
        constexpr uint32_t kPathTraceSamplesPerDispatch = 4;
        constexpr std::size_t kPathTraceMaximumSceneRecords = 512;
        constexpr uint32_t kPathTraceDirectLightSamples = 1;
        constexpr uint32_t kPathTraceDiffuseBounces = 8;
        constexpr uint32_t kPathTraceIntegratorVersion = 4;
        constexpr uint32_t kPathTraceRngSeed = 0x52463436u;
        constexpr uint32_t kPathTraceRngPolicyVersion = 1;
        constexpr uint32_t kPathTraceToneMapOperatorReinhard = 1;
        constexpr uint32_t kPathTraceOutputTransferSrgb = 1;
        constexpr float kPathTraceExposure = 1.0f;
        constexpr float kPathTraceRayMinimumDistance = 0.001f;
        constexpr float kPathTraceRayMaximumDistance = 1000.0f;
        constexpr float kPathTraceSecondaryRayOffset = 0.002f;

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
            std::array<PathTracingGeometryGpuData, kPathTraceMaximumSceneRecords>
                geometry_data{};
            std::array<PathTracingInstanceGpuData, kPathTraceMaximumSceneRecords>
                instance_data{};
            std::array<PathTracingMaterialGpuData, kPathTraceMaximumSceneRecords>
                material_data{};
            std::array<PathTracingLightGpuData, 128> light_data{};
        };

        void AddShaderSignature(uint64_t &signature, const data::ShaderData &shader)
        {
            const auto add = [&signature](uint64_t value) {
                signature ^= value;
                signature *= 1099511628211ull;
            };
            add(static_cast<uint32_t>(shader.stage));
            add(static_cast<uint32_t>(shader.api));
            add(shader.byte_code.size());
            for (const uint8_t byte : shader.byte_code)
            {
                add(byte);
            }
            add(shader.source.size());
            for (const unsigned char character : shader.source)
            {
                add(character);
            }
            for (const unsigned char character : shader.entry)
            {
                add(character);
            }
        }
        static_assert(static_cast<size_t>(FixedRenderPassId::RayTracingPathTrace) ==
                      static_cast<size_t>(RenderProfilePass::RayTracingPathTrace));
        static_assert(static_cast<size_t>(FixedRenderPassId::Count) ==
                      static_cast<size_t>(RenderProfilePass::Count));

        struct PathTracingCameraGpuData
        {
            Matrix4f inverse_view_projection;
            Vector4f camera_position;
            Vector4f light_center;
            Vector4f light_u;
            Vector4f light_v;
            Vector4f light_radiance;
            uint32_t rng_seed = kPathTraceRngSeed;
            uint32_t sample_count = 0;
            uint32_t samples_per_dispatch = 1;
            uint32_t probe_mode = 0;
            uint32_t scene_data[4]{};
        };
        static_assert(offsetof(PathTracingCameraGpuData, rng_seed) == 144);
        static_assert(offsetof(PathTracingCameraGpuData, scene_data) == 160);
        static_assert(sizeof(PathTracingGeometryGpuData) == 32);
        static_assert(sizeof(PathTracingInstanceGpuData) == 16);
        static_assert(sizeof(PathTracingMaterialGpuData) == 64);
        static_assert(sizeof(PathTracingLightGpuData) == 64);
        static_assert(sizeof(PathTracingCameraGpuData) == 176);
        static_assert(sizeof(PathTracingSceneGpuData) ==
                      kPathTraceMaximumSceneRecords * (32 + 16 + 64) + 128 * 64);

        graphics::RenderTargetAttachmentScope ToAttachmentScope(RenderGraphAttachmentScope scope)
        {
            if (scope.all)
            {
                return {};
            }
            return {false, scope.color_mask, scope.depth};
        }

        graphics::ResourceUsage ToResourceUsage(RenderGraphUsage usage)
        {
            switch (usage)
            {
            case RenderGraphUsage::Sampled:
                return graphics::ResourceUsage::Sampled;
            case RenderGraphUsage::ColorAttachment:
                return graphics::ResourceUsage::ColorAttachment;
            case RenderGraphUsage::DepthAttachment:
                return graphics::ResourceUsage::DepthAttachment;
            case RenderGraphUsage::TransferSource:
                return graphics::ResourceUsage::TransferSource;
            case RenderGraphUsage::TransferDestination:
                return graphics::ResourceUsage::TransferDestination;
            case RenderGraphUsage::AccelerationStructureBuildInput:
                return graphics::ResourceUsage::AccelerationStructureBuildInput;
            case RenderGraphUsage::AccelerationStructureBuildOutput:
                return graphics::ResourceUsage::AccelerationStructureBuildOutput;
            case RenderGraphUsage::AccelerationStructureRead:
                return graphics::ResourceUsage::AccelerationStructureRead;
            case RenderGraphUsage::StorageRead:
                return graphics::ResourceUsage::StorageRead;
            case RenderGraphUsage::StorageWrite:
                return graphics::ResourceUsage::StorageWrite;
            case RenderGraphUsage::Present:
                return graphics::ResourceUsage::Present;
            case RenderGraphUsage::Undefined:
                break;
            }
            return graphics::ResourceUsage::Undefined;
        }

        uint64_t GetObjectUniformKey(const RenderableHandle handle)
        {
            return 0x4f424a4543545f55ull ^
                   (static_cast<uint64_t>(handle.id) << 32) ^ handle.generation;
        }

        uint64_t GetSelectionUniformKey(const RenderableHandle handle)
        {
            return 0x53454c4543545f55ull ^
                   (static_cast<uint64_t>(handle.id) << 32) ^ handle.generation;
        }

        bool IsRayTracingVirtualBuffer(std::string_view name) noexcept
        {
            return name == "SceneGeometry" || name == "SceneInstances" ||
                   name == "SceneScratch";
        }

        uint64_t DrawMeshSections(const RenderResourceResolver &resource_resolver,
                                  graphics::CommandRecorder &recorder,
                                  graphics::MeshHandle mesh,
                                  uint32_t section_index = std::numeric_limits<uint32_t>::max())
        {
            uint64_t draw_count = 0;
            const std::vector<data::MeshSection> *const sections =
                resource_resolver.FindMeshSections(mesh);
            if (sections == nullptr || sections->empty() ||
                section_index == std::numeric_limits<uint32_t>::max())
            {
                if (section_index == std::numeric_limits<uint32_t>::max())
                {
                    if (sections == nullptr || sections->empty())
                    {
                        // Preserve the legacy fallback for meshes created
                        // outside the resolver's section cache.
                        recorder.DrawIndexed();
                        return 1;
                    }
                    for (const data::MeshSection &section : *sections)
                    {
                        if (section.index_count != 0)
                        {
                            recorder.DrawIndexed(section.index_count, 1, section.index_start);
                            ++draw_count;
                        }
                    }
                }
                return 0;
            }

            if (section_index < sections->size())
            {
                const data::MeshSection &section = (*sections)[section_index];
                if (section.index_count != 0)
                {
                    recorder.DrawIndexed(section.index_count, 1, section.index_start);
                    ++draw_count;
                }
            }
            return draw_count;
        }

        struct alignas(16) CaptureViewGpuData
        {
            Matrix4f inverse_view_projection;
            Matrix4f view;
            Matrix4f directional_shadow_view_projection;
            Vector4f directional_shadow_params;
            Matrix4f spot_shadow_view_projection;
            Vector4f spot_shadow_params;
            Vector4f light_direction_and_view;
            Vector4f depth_params;
            Vector4f punctual_depth_params;
        };

        struct alignas(16) SelectionGpuData
        {
            Vector4f selected;
        };

        struct DirectionalShadowMatrices
        {
            Matrix4f view;
            Matrix4f projection;
        };

        struct DirectionalShadowFit
        {
            spatial::AABB bounds{};
            DirectionalShadowMatrices matrices{};
        };

        std::optional<spatial::AABB> BuildDirectionalShadowCasterBounds(
            const std::vector<VisibleMeshSection> &sections)
        {
            const float maximum = std::numeric_limits<float>::max();
            spatial::AABB bounds{{maximum, maximum, maximum},
                                 {-maximum, -maximum, -maximum}};
            bool has_caster = false;
            for (const VisibleMeshSection &candidate : sections)
            {
                const MeshProxy &proxy = candidate.proxy;
                if (!proxy.flags.visible || !proxy.flags.casts_shadow ||
                    !candidate.world_bounds.IsValid())
                {
                    continue;
                }
                bounds.ExpandToInclude(candidate.world_bounds.min_);
                bounds.ExpandToInclude(candidate.world_bounds.max_);
                has_caster = true;
            }
            if (!has_caster)
            {
                return std::nullopt;
            }
            return bounds;
        }

        DirectionalShadowMatrices FitDirectionalShadowMatrices(
            const spatial::AABB &caster_bounds, const Vector3f &direction)
        {
            constexpr float kMargin = 5.0f;
            const Vector3f center = (caster_bounds.min_ + caster_bounds.max_) * 0.5f;
            const Vector3f extent = caster_bounds.max_ - caster_bounds.min_;
            const float radius = 0.5f * std::sqrt(extent.SquareLength());
            const Vector3f up = std::abs(direction.y_) > 0.98f
                                    ? Vector3f{0.0f, 0.0f, 1.0f}
                                    : Vector3f{0.0f, 1.0f, 0.0f};
            const Matrix4f view = Matrix4f::MakeCameraMatrix(
                center - direction * (radius + kMargin), direction, up);

            const float maximum = std::numeric_limits<float>::max();
            float min_x = maximum;
            float min_y = maximum;
            float min_z = maximum;
            float max_x = -maximum;
            float max_y = -maximum;
            float max_z = -maximum;
            for (const Vector3f &corner : caster_bounds.GetCorners())
            {
                const Vector4f light_space = view * Vector4f{corner, 1.0f};
                min_x = std::min(min_x, light_space.x_);
                min_y = std::min(min_y, light_space.y_);
                min_z = std::min(min_z, light_space.z_);
                max_x = std::max(max_x, light_space.x_);
                max_y = std::max(max_y, light_space.y_);
                max_z = std::max(max_z, light_space.z_);
            }

            const float near_plane = std::max(0.1f, -max_z - kMargin);
            const float far_plane = std::max(near_plane + 1.0f, -min_z + kMargin);
            return {view, Matrix4f::MakeOrthProjMatrix(min_x - kMargin, max_x + kMargin,
                                                        min_y - kMargin, max_y + kMargin,
                                                        near_plane, far_plane)};
        }

        bool IsPointInsideDirectionalShadowFit(const DirectionalShadowMatrices &matrices,
                                               const Vector3f &point)
        {
            constexpr float kContainmentEpsilon = 1.0e-4f;
            const Vector4f clip = matrices.projection *
                                  (matrices.view * Vector4f{point, 1.0f});
            return std::abs(clip.x_) <= 1.0f + kContainmentEpsilon &&
                   std::abs(clip.y_) <= 1.0f + kContainmentEpsilon &&
                   std::abs(clip.z_) <= 1.0f + kContainmentEpsilon;
        }

        std::optional<DirectionalShadowFit> BuildEffectiveDirectionalShadowFit(
            const std::vector<VisibleMeshSection> &sections,
            const Vector3f &camera_position, const Vector3f &direction)
        {
            const std::optional<spatial::AABB> caster_bounds =
                BuildDirectionalShadowCasterBounds(sections);
            if (!caster_bounds.has_value())
            {
                return std::nullopt;
            }

            DirectionalShadowFit fit{*caster_bounds,
                                     FitDirectionalShadowMatrices(*caster_bounds, direction)};
            if (!IsPointInsideDirectionalShadowFit(fit.matrices, camera_position))
            {
                fit.bounds.ExpandToInclude(camera_position);
                fit.matrices = FitDirectionalShadowMatrices(fit.bounds, direction);
            }
            return fit;
        }

        bool IsSpotBoundsInsideFrustum(const spatial::AABB &bounds,
                                       const Matrix4f &view,
                                       float outer_cone_radians,
                                       float near_plane,
                                       float far_plane)
        {
            if (!bounds.IsValid())
            {
                return true;
            }
            const float tangent = std::tan(outer_cone_radians);
            for (const Vector3f &corner : bounds.GetCorners())
            {
                const Vector4f light_space = view * Vector4f{corner, 1.0f};
                const float depth = -light_space.z_;
                if (depth >= near_plane && depth <= far_plane &&
                    std::abs(light_space.x_) <= depth * tangent &&
                    std::abs(light_space.y_) <= depth * tangent)
                {
                    return true;
                }
            }
            return false;
        }

        bool IsBoundsInsideSphere(const spatial::AABB &bounds, const Vector3f &center,
                                  float radius)
        {
            if (!bounds.IsValid())
            {
                return true;
            }
            const Vector3f closest{std::max(bounds.min_.x_, std::min(center.x_, bounds.max_.x_)),
                                   std::max(bounds.min_.y_, std::min(center.y_, bounds.max_.y_)),
                                   std::max(bounds.min_.z_, std::min(center.z_, bounds.max_.z_))};
            const Vector3f delta = closest - center;
            return delta.SquareLength() <= radius * radius;
        }

        uint64_t ComputeDirectionalShadowStamp(
            const Light &light, const std::vector<VisibleMeshSection> &sections,
            const DirectionalShadowFit &fit)
        {
            uint64_t stamp = 1469598103934665603ULL;
            const auto add = [&stamp](uint64_t value)
            {
                stamp ^= value;
                stamp *= 1099511628211ULL;
            };
            const auto add_float = [&add](float value)
            { add(static_cast<uint64_t>(std::hash<float>{}(value))); };
            const auto add_vector = [&add_float](const Vector3f &value)
            {
                add_float(value.x_);
                add_float(value.y_);
                add_float(value.z_);
            };
            add(light.handle.id);
            add(light.handle.generation);
            add(light.desc.shadow->id);
            add(light.desc.shadow->generation);
            add_vector(std::get<DirectionalLightData>(light.desc.type_data).direction);
            add(fit.bounds.IsValid() ? 1u : 0u);
            if (fit.bounds.IsValid())
            {
                add_vector(fit.bounds.min_);
                add_vector(fit.bounds.max_);
            }
            const auto add_matrix = [&add_float](const Matrix4f &matrix)
            {
                for (size_t row = 0; row < 4; ++row)
                {
                    for (size_t column = 0; column < 4; ++column)
                    {
                        add_float(matrix[row][column]);
                    }
                }
            };
            add_matrix(fit.matrices.view);
            add_matrix(fit.matrices.projection);
            for (const VisibleMeshSection &candidate : sections)
            {
                const MeshProxy &proxy = candidate.proxy;
                add(proxy.handle.id);
                add(proxy.handle.generation);
                add(proxy.mesh.id);
                add(proxy.mesh.generation);
                add(proxy.material.id);
                add(proxy.material.generation);
                add(candidate.section_index);
                add(proxy.flags.visible ? 1u : 0u);
                add(proxy.flags.casts_shadow ? 1u : 0u);
                add_vector(proxy.world_transform.position_);
                add_vector(proxy.world_transform.scale_);
                add_float(proxy.world_transform.rotator_.pitch_);
                add_float(proxy.world_transform.rotator_.yaw_);
                add_float(proxy.world_transform.rotator_.roll_);
                add_vector(candidate.world_bounds.min_);
                add_vector(candidate.world_bounds.max_);
            }
            return stamp;
        }
    }

    DeferredRenderer::~DeferredRenderer()
    {
        Cleanup();
    }

    DeferredRendererInitResult DeferredRenderer::Initialize(const DeferredRendererInitInfo &info,
                                                            uint32_t width, uint32_t height)
    {
        if (backend_ != nullptr)
        {
            return {false, "DeferredRenderer can only be initialized once."};
        }
        if (width == 0 || height == 0)
        {
            return {false, "DeferredRenderer requires a non-zero render extent."};
        }

        backend_ = &info.backend;
        resource_resolver_ = &info.resource_resolver;
        material_system_ = &info.materials;
        prepared_assets_ = &info.prepared_assets;
        path_tracing_enabled_ = info.path_tracing_enabled;
        try
        {
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "R4.6 initializing frame targets");
            frame_targets_.Initialize(*backend_, width, height);
            if (!frame_targets_.IsValid())
            {
                throw std::runtime_error("Failed to create the complete render target set.");
            }
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "R4.6 frame targets initialized");
            const bool r46_pipeline_ready = path_tracing_enabled_ &&
                backend_->GetCapabilities().SupportsRayTracingPipeline() &&
                PrepareRayTracingPathTraceResources();
            ray_tracing_path_tracing_available_ = r46_pipeline_ready;
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "R4.6 path tracing resource preparation completed");
            ConfigureFramePlans();
            if (!frame_plan_valid_)
            {
                throw std::runtime_error("Render graph frame plan compilation failed.");
            }
            return {true, {}};
        }
        catch (const std::exception &error)
        {
            const std::string diagnostic = error.what();
            Cleanup();
            return {false, diagnostic};
        }
        catch (...)
        {
            Cleanup();
            return {false, "Unknown exception during DeferredRenderer initialization."};
        }
    }

    void DeferredRenderer::Cleanup()
    {
        uint32_t allocated_history_targets = 0;
        for (const auto &target : path_trace_history_targets_)
        {
            allocated_history_targets += target ? 1u : 0u;
        }
        // Drop the adopted transient before the backend tears its pool down, so
        // no wrapper outlives the handle it borrows.
        ReleaseFrameTransients();
        transient_scene_hdr_.reset();
        graphics::RayTracingResourceOwner *const ray_tracing_owner =
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
        if (ray_tracing_owner && ray_tracing_path_tracing_bindings_.IsValid())
        {
            ray_tracing_owner->DestroyRayTracingResourceBindingSet(
                ray_tracing_path_tracing_bindings_);
        }
        ray_tracing_path_tracing_bindings_ = {};
        DestroyRayTracingResources();
        // RT bindings and acceleration structures are retired against submitted
        // work; wait before releasing their referenced targets or pipeline.
        if (backend_ != nullptr)
            backend_->WaitIdle();
        for (auto &target : path_trace_history_targets_)
        {
            if (target) target->Cleanup();
            target.reset();
        }
        uint32_t remaining_history_targets = 0;
        for (const auto &target : path_trace_history_targets_)
        {
            remaining_history_targets += target ? 1u : 0u;
        }
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "R4.6 history-target teardown: released=%u, remaining=%u",
               allocated_history_targets - remaining_history_targets,
               remaining_history_targets);
        path_trace_sample_count_ = 0;
        path_trace_write_index_ = 0;
        path_trace_history_signature_ = 0;
        path_trace_shader_signature_ = 0;
        tone_map_shader_signature_ = 0;
        if (ray_tracing_owner && ray_tracing_path_tracing_pipeline_.IsValid())
            ray_tracing_owner->DestroyRayTracingPipeline(ray_tracing_path_tracing_pipeline_);
        if (backend_ != nullptr)
        {
            if (gbuffer_debug_pipeline_.IsValid())
                backend_->DestroyPipelineResource(gbuffer_debug_pipeline_);
            if (capture_view_pipeline_.IsValid())
                backend_->DestroyPipelineResource(capture_view_pipeline_);
            if (deferred_lighting_pipeline_.IsValid())
                backend_->DestroyPipelineResource(deferred_lighting_pipeline_);
            if (deferred_lighting_ray_query_pipeline_.IsValid())
                backend_->DestroyPipelineResource(deferred_lighting_ray_query_pipeline_);
            if (gbuffer_debug_fullscreen_mesh_.IsValid())
                backend_->DestroyMesh(gbuffer_debug_fullscreen_mesh_);
            if (gbuffer_debug_sampler_.IsValid())
                backend_->DestroySampler(gbuffer_debug_sampler_);
            if (directional_shadow_sampler_.IsValid())
                backend_->DestroySampler(directional_shadow_sampler_);
            if (spot_shadow_sampler_.IsValid())
                backend_->DestroySampler(spot_shadow_sampler_);
            if (point_shadow_sampler_.IsValid())
                backend_->DestroySampler(point_shadow_sampler_);
            if (tone_map_pipeline_.IsValid())
                backend_->DestroyPipelineResource(tone_map_pipeline_);
            if (directional_shadow_pipeline_.IsValid())
                backend_->DestroyPipelineResource(directional_shadow_pipeline_);
        }
        gbuffer_debug_pipeline_ = {};
        capture_view_pipeline_ = {};
        deferred_lighting_pipeline_ = {};
        deferred_lighting_ray_query_pipeline_ = {};
        ray_query_shadow_path_active_ = false;
        gbuffer_debug_fullscreen_mesh_ = {};
        gbuffer_debug_sampler_ = {};
        directional_shadow_sampler_ = {};
        spot_shadow_sampler_ = {};
        point_shadow_sampler_ = {};
        tone_map_pipeline_ = {};
        directional_shadow_pipeline_ = {};
        ray_tracing_path_tracing_pipeline_ = {};
        ray_tracing_path_tracing_bindings_ = {};
        ray_tracing_path_tracing_available_ = false;
        active_ray_tracing_path_trace_ = false;
        level_environment_ = {};
        active_environment_ = {};
        failed_environment_source_.reset();
        active_directional_shadow_.reset();
        active_spot_shadow_.reset();
        active_point_shadow_.reset();
        frame_lighting_binding_ = {};
        spot_shadow_recorded_ = false;
        point_shadow_recorded_ = false;
        directional_shadow_cache_hit_ = false;
        directional_shadow_valid_ = false;
        directional_shadow_stamp_ = 0;
        active_frame_context_ = nullptr;
        render_world_ = nullptr;
        frame_render_world_snapshot_.clear();
        frame_section_packets_.clear();
        frame_section_packets_ready_ = false;
        frame_ray_tracing_geometries_.clear();
        frame_ray_tracing_instances_.clear();
        frame_ray_tracing_instance_data_.clear();
        frame_ray_tracing_material_data_.clear();
        frame_ray_tracing_light_data_.clear();
        frame_ray_tracing_lighting_signature_ = 0;
        frame_ray_tracing_mesh_builds_.clear();
        frame_ray_tracing_blas_builds_.clear();
        frame_ray_tracing_tlas_builds_.clear();
        frame_ray_tracing_material_signature_ = 0;
        frame_ray_tracing_blas_build_ = false;
        frame_ray_tracing_tlas_build_ = false;
        frame_object_states_.clear();
        frame_material_bindings_.clear();
        pending_scene_render_target_extent_ = {};
        active_pass_frame_.reset();
        active_frame_plan_ = nullptr;
        active_pending_capture_.reset();
        for (std::optional<RenderGraphCompileResult> &plan : frame_plans_)
        {
            plan.reset();
        }
        frame_plan_valid_ = false;
        frame_plan_compile_ms_ = 0.0;
        frame_targets_.Cleanup();
        backend_ = nullptr;
        resource_resolver_ = nullptr;
        material_system_ = nullptr;
        prepared_assets_ = nullptr;
    }

    bool DeferredRenderer::GetPreparedProgram(BuiltInRenderAsset role,
                                              std::shared_ptr<const asset::ShaderProgramResource> &out_program,
                                              asset::ShaderProgramVariant variant) const
    {
        out_program = prepared_assets_ != nullptr
                          ? prepared_assets_->Get<asset::ShaderProgramResource>(
                                prepared_assets_->GetBuiltIn(role))
                          : nullptr;
        if (!out_program)
        {
            return false;
        }
        for (const ShaderStage stage : {ShaderStage::SHADER_STAGE_VERTEX,
                                       ShaderStage::SHADER_STAGE_FRAGMENT})
        {
            const asset::AssetID shader_id = out_program->GetData(
                stage, ShaderFormat::SHADER_FORMAT_GLSL,
                variant);
            const auto shader = prepared_assets_->Get<asset::ShaderResource>(shader_id);
            if (!shader || !shader->data || shader->status != asset::ShaderStatus::Ready)
            {
                out_program.reset();
                return false;
            }
        }
        return true;
    }

    void DeferredRenderer::RequestExtent(uint32_t width, uint32_t height)
    {
        if (width != 0 && height != 0)
        {
            pending_scene_render_target_extent_ = {width, height};
        }
    }

    const std::vector<VisibleMeshSection> &DeferredRenderer::BuildSectionCandidatesProfiled()
    {
        if (!frame_section_packets_ready_)
        {
            const auto started = std::chrono::steady_clock::now();
            frame_section_packets_ = SceneVisibility::BuildSectionCandidates(
                frame_render_world_snapshot_, *resource_resolver_);
            profile_.cpu_section_packet_build_ms +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            ++profile_.section_packet_build_calls;
            profile_.section_packets_built += frame_section_packets_.size();
            frame_section_packets_ready_ = true;
        }
        return frame_section_packets_;
    }

    std::vector<VisibleMeshSection> DeferredRenderer::BuildVisibleSectionsProfiled(
        const Matrix4f &view_projection)
    {
        const auto started = std::chrono::steady_clock::now();
        const std::vector<VisibleMeshSection> &section_packets =
            BuildSectionCandidatesProfiled();
        std::vector<VisibleMeshSection> result =
            SceneVisibility::FilterVisibleSections(view_projection, section_packets);
        profile_.cpu_section_packet_build_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        ++profile_.section_packet_build_calls;
        profile_.section_packets_built += result.size();
        return result;
    }

    void DeferredRenderer::ApplyPendingExtent()
    {
        ApplyPendingSceneRenderTargetExtent();
    }

    const RenderTarget &DeferredRenderer::GetSceneRenderTarget() const
    {
        static const RenderTarget empty_target;
        const RenderTarget *const scene_target =
            frame_targets_.GetTarget(RenderTargetName::SceneColor);
        return scene_target ? *scene_target : empty_target;
    }

    spatial::Ray DeferredRenderer::BuildSceneRay(float ndc_x, float ndc_y,
                                                  float viewport_aspect) const
    {
        return scene_camera_.BuildWorldRay(ndc_x, ndc_y, viewport_aspect);
    }

    std::optional<Vector3f> DeferredRenderer::ProjectScenePoint(
        const Vector3f &world_point, float viewport_aspect) const
    {
        if (viewport_aspect <= 0.0f)
        {
            return std::nullopt;
        }

        RenderCamera camera = scene_camera_;
        camera.SetAspect(viewport_aspect);
        const Vector4f clip = camera.GetViewProjectionMatrix() * Vector4f(world_point, 1.0f);
        if (clip.w_ <= 0.0001f)
        {
            return std::nullopt;
        }

        const float inverse_w = 1.0f / clip.w_;
        return Vector3f{clip.x_ * inverse_w, clip.y_ * inverse_w, clip.z_ * inverse_w};
    }

    graphics::RenderTargetView DeferredRenderer::GetViewportRenderTargetView(
        CaptureView view) const
    {
        const RenderTargetName target_name = view == CaptureView::SceneColor
                                                 ? RenderTargetName::SceneColor
                                                 : RenderTargetName::CaptureOutput;
        const RenderTarget *const target = frame_targets_.GetTarget(target_name);
        return target ? target->GetView() : graphics::RenderTargetView{};
    }

    graphics::RenderTargetHandle DeferredRenderer::GetCaptureTarget(CaptureView view) const
    {
        const RenderTargetName target_name = view == CaptureView::SceneColor
                                                 ? RenderTargetName::SceneColor
                                                 : RenderTargetName::CaptureOutput;
        const RenderTarget *const target = frame_targets_.GetTarget(target_name);
        return target ? target->GetHandle() : graphics::RenderTargetHandle{};
    }

    void DeferredRenderer::SetPathTraceProbeMode(PathTraceProbeMode mode)
    {
        path_trace_probe_mode_ = mode;
    }

    void DeferredRenderer::InjectNextPathTraceDispatchFailure()
    {
        fail_next_path_trace_dispatch_ = true;
    }

    bool DeferredRenderer::PrepareRayTracingScene()
    {
        frame_ray_tracing_geometries_.clear();
        frame_ray_tracing_instances_.clear();
        frame_ray_tracing_instance_data_.clear();
        frame_ray_tracing_material_data_.clear();
        frame_ray_tracing_light_data_.clear();
        frame_ray_tracing_lighting_signature_ = 0;
        frame_ray_tracing_mesh_builds_.clear();
        frame_ray_tracing_blas_builds_.clear();
        frame_ray_tracing_tlas_builds_.clear();
        frame_ray_tracing_material_signature_ = 0;
        frame_ray_tracing_blas_build_ = false;
        frame_ray_tracing_tlas_build_ = false;

        graphics::RayTracingResourceOwner *const owner =
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
        if (owner == nullptr || !owner->IsSupported())
        {
            return true;
        }

        uint64_t instance_signature = 1469598103934665603ull;
        uint64_t material_signature = 1469598103934665603ull;
        const auto add_signature = [&instance_signature](uint64_t value) {
            instance_signature ^= value;
            instance_signature *= 1099511628211ull;
        };
        const auto add_material_signature = [&material_signature](uint64_t value) {
            material_signature ^= value;
            material_signature *= 1099511628211ull;
        };
        const auto add_float = [&add_signature](float value) {
            add_signature(static_cast<uint64_t>(std::hash<float>{}(value)));
        };

        std::unordered_map<graphics::MeshHandle, std::size_t> mesh_indices;
        for (const MeshProxy &proxy : frame_render_world_snapshot_)
        {
            if (!proxy.flags.visible || !proxy.mesh.IsValid())
            {
                continue;
            }

            const auto [mesh_iterator, inserted] = mesh_indices.emplace(
                proxy.mesh, frame_ray_tracing_mesh_builds_.size());
            if (inserted)
            {
                std::vector<graphics::RayTracingGeometryDesc> geometries =
                    backend_->GetRayTracingGeometry(proxy.mesh);
                if (geometries.empty())
                {
                    mesh_indices.erase(mesh_iterator);
                    continue;
                }

                const uint64_t geometry_signature =
                    detail::RayTracingGeometrySignature(geometries);
                RayTracingBlasState &state = ray_tracing_blas_[proxy.mesh];
                if (state.handle.IsValid() &&
                    (state.geometry_count != geometries.size() ||
                     state.geometry_signature != geometry_signature))
                {
                    owner->DestroyAccelerationStructure(state.handle);
                    state = {};
                }
                if (!state.handle.IsValid())
                {
                    state.handle = owner->CreateAccelerationStructure(
                        {graphics::RayTracingAccelerationStructureType::BottomLevel,
                         static_cast<uint32_t>(geometries.size()), 0, false});
                    state.geometry_count = static_cast<uint32_t>(geometries.size());
                    state.geometry_signature = geometry_signature;
                    state.built = false;
                }
                if (!state.handle.IsValid())
                {
                    mesh_indices.erase(mesh_iterator);
                    continue;
                }

                const std::size_t geometry_offset = frame_ray_tracing_geometries_.size();
                frame_ray_tracing_geometries_.insert(frame_ray_tracing_geometries_.end(),
                                                     geometries.begin(), geometries.end());
                frame_ray_tracing_mesh_builds_.push_back(
                    {proxy.mesh, state.handle, geometry_offset, geometries.size(),
                     geometry_signature, !state.built});
                if (!state.built)
                {
                    frame_ray_tracing_blas_build_ = true;
                }
            }

            const auto build_iterator = mesh_indices.find(proxy.mesh);
            if (build_iterator == mesh_indices.end())
            {
                continue;
            }
            const RayTracingMeshBuild &mesh_build =
                frame_ray_tracing_mesh_builds_[build_iterator->second];
            graphics::RayTracingInstanceDesc instance{};
            instance.bottom_level = mesh_build.blas;
            instance.instance_id = static_cast<uint32_t>(frame_ray_tracing_instances_.size());
            const Matrix4f transform = Matrix4f::MakeTransformMatrix(proxy.world_transform);
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t column = 0; column < 4; ++column)
                {
                    instance.transform[row * 4 + column] = transform[row][column];
                    add_float(transform[row][column]);
                }
            }
            add_signature(proxy.handle.id);
            add_signature(proxy.handle.generation);
            add_signature(proxy.mesh.id);
            add_signature(proxy.mesh.generation);
            add_signature(mesh_build.geometry_signature);
            add_signature(mesh_build.blas.id);
            add_signature(mesh_build.blas.generation);
            if (path_tracing_enabled_)
            {
                const std::vector<data::MeshSection> *const sections =
                    resource_resolver_ != nullptr
                        ? resource_resolver_->FindMeshSections(proxy.mesh)
                        : nullptr;
                const std::size_t material_offset = frame_ray_tracing_material_data_.size();
                for (std::size_t geometry = 0; geometry < mesh_build.geometry_count; ++geometry)
                {
                    RayTracingPathMaterialData material_data{};
                    MaterialInstanceHandle material = proxy.material;
                    if (sections != nullptr && geometry < sections->size())
                    {
                        material = proxy.GetMaterialForSection((*sections)[geometry].material_index);
                    }
                    if (material_system_ != nullptr && material.IsValid())
                    {
                    const MaterialTemplateHandle template_handle =
                        material_system_->GetInstanceTemplate(material);
                    const auto read_parameter = [this, material, template_handle](
                        std::string_view name) -> const MaterialParameterValue * {
                        const MaterialParameterID parameter =
                            material_system_->FindParameterID(template_handle, name);
                        return material_system_->GetParameterValue(material, parameter);
                    };
                    if (const MaterialParameterValue *value = read_parameter("base_color"))
                    {
                        if (const Vector4f *color = std::get_if<Vector4f>(value))
                        {
                            material_data.base_color = *color;
                        }
                    }
                    if (const MaterialParameterValue *value = read_parameter("emissive"))
                    {
                        if (const Vector4f *emissive = std::get_if<Vector4f>(value))
                        {
                            material_data.emissive = *emissive;
                        }
                    }
                    if (const MaterialParameterValue *value = read_parameter("metallic"))
                    {
                        if (const float *metallic = std::get_if<float>(value))
                        {
                            material_data.metallic = *metallic;
                        }
                    }
                    if (const MaterialParameterValue *value = read_parameter("roughness"))
                    {
                        if (const float *roughness = std::get_if<float>(value))
                        {
                            material_data.roughness = *roughness;
                        }
                    }
                    const MaterialParameterID texture_parameter =
                        material_system_->FindParameterID(template_handle, "base_color_texture");
                    if (texture_parameter.IsValid() && resource_resolver_ != nullptr)
                    {
                        if (const MaterialParameterValue *texture_value =
                                material_system_->GetParameterValue(material, texture_parameter))
                        {
                            if (const auto *texture =
                                    std::get_if<MaterialTextureSamplerValue>(texture_value))
                            {
                                add_material_signature(texture->texture_asset.Pack());
                            }
                        }
                        if (const auto *textures = resource_resolver_->FindTextureBindings(material))
                        {
                            const auto slot =
                                textures->ray_tracing_bindless_slots.find(texture_parameter.value);
                            if (slot != textures->ray_tracing_bindless_slots.end() &&
                                slot->second.IsValid())
                            {
                                material_data.base_color_texture_index = slot->second.id;
                            }
                        }
                    }
                    const auto resolve_scalar_texture = [&](std::string_view name) {
                        const MaterialParameterID parameter =
                            material_system_->FindParameterID(template_handle, name);
                        uint32_t index = 0xffffffffu;
                        if (parameter.IsValid() && resource_resolver_ != nullptr)
                        {
                            if (const auto *textures = resource_resolver_->FindTextureBindings(material))
                            {
                                const auto slot = textures->ray_tracing_bindless_slots.find(parameter.value);
                                if (slot != textures->ray_tracing_bindless_slots.end() && slot->second.IsValid())
                                    index = slot->second.id;
                            }
                            if (const auto *value = material_system_->GetParameterValue(material, parameter))
                            {
                                if (const auto *texture = std::get_if<MaterialTextureSamplerValue>(value))
                                    add_material_signature(texture->texture_asset.Pack());
                            }
                        }
                        return index;
                    };
                    material_data.metallic_texture_index = resolve_scalar_texture("metallic_texture");
                    material_data.roughness_texture_index = resolve_scalar_texture("roughness_texture");
                    if (const auto *value = read_parameter("texture_channels"))
                    {
                        if (const auto *channels = std::get_if<Vector4f>(value))
                        {
                            material_data.metallic_channel = static_cast<uint32_t>(std::clamp(channels->x_, 0.0f, 3.0f));
                            material_data.roughness_channel = static_cast<uint32_t>(std::clamp(channels->y_, 0.0f, 3.0f));
                        }
                    }
                    add_material_signature(material.id);
                    add_material_signature(material.generation);
                    add_material_signature(material_system_->GetInstanceRevision(material));
                }
                const auto add_material_color = [&add_material_signature](float component) {
                    add_material_signature(static_cast<uint64_t>(std::hash<float>{}(component)));
                };
                add_material_color(material_data.base_color.x_);
                add_material_color(material_data.base_color.y_);
                add_material_color(material_data.base_color.z_);
                add_material_color(material_data.emissive.x_);
                add_material_color(material_data.emissive.y_);
                add_material_color(material_data.emissive.z_);
                add_material_color(material_data.metallic);
                add_material_color(material_data.roughness);
                add_material_signature(material_data.base_color_texture_index);
                add_material_signature(material_data.metallic_texture_index);
                add_material_signature(material_data.roughness_texture_index);
                add_material_signature(material_data.metallic_channel);
                add_material_signature(material_data.roughness_channel);
                frame_ray_tracing_material_data_.push_back(material_data);
            }
                frame_ray_tracing_instance_data_.push_back(
                    {static_cast<uint32_t>(mesh_build.geometry_offset),
                     static_cast<uint32_t>(material_offset),
                     static_cast<uint32_t>(mesh_build.geometry_count)});
            }
            frame_ray_tracing_instances_.push_back(instance);
        }

        if (frame_ray_tracing_instances_.empty())
        {
            frame_ray_tracing_instance_signature_ = instance_signature;
            frame_ray_tracing_material_signature_ = material_signature;
            return true;
        }
        frame_ray_tracing_instance_signature_ = instance_signature;
        frame_ray_tracing_material_signature_ = material_signature;

        if (!ray_tracing_tlas_.IsValid() ||
            ray_tracing_tlas_capacity_ < frame_ray_tracing_instances_.size())
        {
            if (ray_tracing_tlas_.IsValid())
            {
                owner->DestroyAccelerationStructure(ray_tracing_tlas_);
            }
            ray_tracing_tlas_ = owner->CreateAccelerationStructure(
                {graphics::RayTracingAccelerationStructureType::TopLevel, 0,
                 static_cast<uint32_t>(frame_ray_tracing_instances_.size()), true});
            ray_tracing_tlas_capacity_ =
                static_cast<uint32_t>(frame_ray_tracing_instances_.size());
            ray_tracing_tlas_built_ = false;
        }
        if (!ray_tracing_tlas_.IsValid())
        {
            return true;
        }

        for (const RayTracingMeshBuild &mesh_build : frame_ray_tracing_mesh_builds_)
        {
            if (mesh_build.needs_build)
            {
                frame_ray_tracing_blas_builds_.push_back(
                    {mesh_build.blas, graphics::RayTracingBuildMode::Build,
                     std::span<const graphics::RayTracingGeometryDesc>(
                         frame_ray_tracing_geometries_.data() + mesh_build.geometry_offset,
                         mesh_build.geometry_count),
                     {}});
            }
        }

        frame_ray_tracing_tlas_build_ =
            !ray_tracing_tlas_built_ || instance_signature != ray_tracing_instance_signature_;
        if (frame_ray_tracing_tlas_build_)
        {
            frame_ray_tracing_tlas_builds_.push_back(
                {ray_tracing_tlas_,
                 ray_tracing_tlas_built_ ? graphics::RayTracingBuildMode::Update
                                         : graphics::RayTracingBuildMode::Build,
                 {},
                 std::span<const graphics::RayTracingInstanceDesc>(
                     frame_ray_tracing_instances_.data(), frame_ray_tracing_instances_.size())});
        }
        return true;
    }

    bool DeferredRenderer::GetPreparedRayTracingProgram(
        std::shared_ptr<const asset::ShaderProgramResource> &out_program) const
    {
        out_program = prepared_assets_ != nullptr
                          ? prepared_assets_->Get<asset::ShaderProgramResource>(
                                prepared_assets_->GetBuiltIn(
                                    BuiltInRenderAsset::RayTracingPathTracerProgram))
                          : nullptr;
        if (!out_program)
        {
            return false;
        }
        for (const ShaderStage stage : {ShaderStage::SHADER_STAGE_RAYGEN,
                                        ShaderStage::SHADER_STAGE_MISS,
                                        ShaderStage::SHADER_STAGE_CLOSEST_HIT})
        {
            const asset::AssetID shader_id = out_program->GetData(
                stage, ShaderFormat::SHADER_FORMAT_GLSL);
            const auto shader = prepared_assets_->Get<asset::ShaderResource>(shader_id);
            if (!shader || !shader->data || shader->status != asset::ShaderStatus::Ready ||
                shader->data->api != GraphicsAPIType::GRAPHICS_API_VULKAN ||
                shader->data->byte_code.empty())
            {
                out_program.reset();
                return false;
            }
        }
        return true;
    }

    bool DeferredRenderer::PrepareRayTracingPathTraceResources()
    {
        if (ray_tracing_path_tracing_pipeline_.IsValid())
        {
            return true;
        }
        if (!backend_ || !backend_->GetCapabilities().SupportsRayTracingPipeline() ||
            !backend_->GetCapabilities().SupportsBindlessTextures())
        {
            return false;
        }
        graphics::RayTracingResourceOwner *const owner =
            backend_->GetRayTracingResourceOwner();
        if (!owner)
        {
            return false;
        }
        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedRayTracingProgram(program))
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "R4.6 path tracing is unavailable: Vulkan RT shader program is not ready");
            return false;
        }
        const auto raygen = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_RAYGEN, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto miss = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_MISS, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto closest_hit = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_CLOSEST_HIT, ShaderFormat::SHADER_FORMAT_GLSL));
        graphics::RayTracingPipelineDesc desc{};
        desc.ray_generation_shader = raygen->data.get();
        desc.miss_shader = miss->data.get();
        desc.closest_hit_shader = closest_hit->data.get();
        path_trace_shader_signature_ = 1469598103934665603ull;
        AddShaderSignature(path_trace_shader_signature_, *raygen->data);
        AddShaderSignature(path_trace_shader_signature_, *miss->data);
        AddShaderSignature(path_trace_shader_signature_, *closest_hit->data);
        desc.max_recursion_depth = 1;
        desc.descriptor_binding_descs = {{
            {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_STORAGE_IMAGE,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_CLOSEST_HIT},
            {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_MISS},
            {35, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_STORAGE_IMAGE,
             ShaderStage::SHADER_STAGE_RAYGEN},
        }};
        desc.descriptor_binding_descs.emplace_back();
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "R4.6 creating Vulkan ray-tracing pipeline");
        ray_tracing_path_tracing_pipeline_ = owner->CreateRayTracingPipeline(desc);
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "R4.6 Vulkan ray-tracing pipeline creation returned");
        if (!ray_tracing_path_tracing_pipeline_.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "R4.6 path tracing is unavailable: Vulkan RT pipeline creation failed");
            return false;
        }
        return true;
    }

    bool DeferredRenderer::RecordRayTracingBlasBuild()
    {
        graphics::CommandRecorder *const recorder =
            backend_ != nullptr ? backend_->GetCommandRecorder() : nullptr;
        if (recorder == nullptr || frame_ray_tracing_blas_builds_.empty())
        {
            return false;
        }
        if (!recorder->BuildAccelerationStructures(frame_ray_tracing_blas_builds_))
        {
            return false;
        }
        for (const RayTracingMeshBuild &mesh_build : frame_ray_tracing_mesh_builds_)
        {
            if (mesh_build.needs_build)
            {
                const auto iterator = ray_tracing_blas_.find(mesh_build.mesh);
                if (iterator != ray_tracing_blas_.end())
                {
                    iterator->second.built = true;
                }
            }
        }
        return true;
    }

    bool DeferredRenderer::RecordRayTracingTlasBuild()
    {
        graphics::CommandRecorder *const recorder =
            backend_ != nullptr ? backend_->GetCommandRecorder() : nullptr;
        if (recorder == nullptr || frame_ray_tracing_tlas_builds_.empty())
        {
            return false;
        }
        if (!recorder->BuildAccelerationStructures(frame_ray_tracing_tlas_builds_))
        {
            return false;
        }
        ray_tracing_tlas_built_ = true;
        ray_tracing_instance_signature_ = frame_ray_tracing_instance_signature_;
        return true;
    }

    void DeferredRenderer::DestroyRayTracingResources()
    {
        graphics::RayTracingResourceOwner *const owner =
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
        if (owner == nullptr)
        {
            ray_tracing_blas_.clear();
            ray_tracing_tlas_ = {};
            return;
        }
        for (const auto &[mesh, state] : ray_tracing_blas_)
        {
            (void)mesh;
            if (state.handle.IsValid())
            {
                owner->DestroyAccelerationStructure(state.handle);
            }
        }
        if (ray_tracing_tlas_.IsValid())
        {
            owner->DestroyAccelerationStructure(ray_tracing_tlas_);
        }
        ray_tracing_blas_.clear();
        ray_tracing_tlas_ = {};
        ray_tracing_tlas_capacity_ = 0;
        ray_tracing_tlas_built_ = false;
        ray_tracing_instance_signature_ = 0;
    }

    DeferredRendererFrameResult DeferredRenderer::RecordFrame(
        FrameContext &frame_context, const RenderSceneFrameInput &input)
    {
        DeferredRendererFrameResult result{};
        triangle_count_ = 0;
        profile_ = {};
        profile_.graph_compile_ms = frame_plan_compile_ms_;
        profile_.frame_number = frame_context.GetGlobals().frame_number;
        profile_.graphics_api = backend_->GetGraphicsAPI();
        profile_.path_tracing_enabled = path_tracing_enabled_;
        profile_.path_tracing_available = ray_tracing_path_tracing_available_;
        profile_.ray_query_shadows_available =
            backend_->GetCapabilities().SupportsRayQueryShadows();
        profile_.viewport_width = frame_context.GetRenderExtent().width;
        profile_.viewport_height = frame_context.GetRenderExtent().height;
        profile_.textures = resource_resolver_->GetTextureMetrics();
        material_system_->ResetProfileCounters();
        if (!frame_plan_valid_ || active_pass_frame_.has_value())
        {
            result.normal_recording_completed = false;
            return result;
        }
        active_frame_context_ = &frame_context;
        render_world_ = &input.render_world;
        const auto render_world_snapshot_started = std::chrono::steady_clock::now();
        frame_render_world_snapshot_ = render_world_->Snapshot();
        profile_.cpu_render_world_snapshot_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - render_world_snapshot_started)
                .count();
        frame_section_packets_.clear();
        frame_section_packets_ready_ = false;
        frame_object_states_.clear();
        frame_material_bindings_.clear();
        scene_camera_ = input.camera;
        // Only a view this renderer converts itself is recorded here. A
        // host-resolved view is satisfied by the host's own target instead.
        const bool pending_needs_conversion =
            input.pending_capture.has_value() &&
            RequiresCaptureViewConversionPass(*input.pending_capture);
        // RenderSystem passes a debug view only when it is not SceneColor, so
        // any debug view reaching here is one this renderer converts.
        const bool debug_needs_conversion = input.debug_view.has_value();
        // A pending capture does not shadow the debug view. The editor samples
        // its own view independently of any capture request, so a SceneColor
        // capture alongside a conversion debug view still needs the pass:
        // otherwise the plan drops it while the host keeps sampling the target
        // it writes, and no declared read remains to move that target's state.
        std::optional<CaptureView> conversion_view;
        if (pending_needs_conversion)
        {
            conversion_view = input.pending_capture;
        }
        else if (debug_needs_conversion)
        {
            conversion_view = input.debug_view;
        }
        active_pending_capture_ = conversion_view;
        const bool is_deferred_capture = conversion_view.has_value();
        UpdateEnvironment(input);
        const auto shadow_stamp_fit_started = std::chrono::steady_clock::now();
        active_directional_shadow_ = ScheduleDirectionalShadow(input.lights,
                                                                input.is_shadow_handle_valid);
        profile_.cpu_shadow_stamp_fit_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - shadow_stamp_fit_started)
                .count();
        directional_shadow_cache_hit_ =
            active_directional_shadow_.has_value() && directional_shadow_valid_ &&
            active_directional_shadow_->validity_stamp == directional_shadow_stamp_;
        profile_.shadow_cache_hits = directional_shadow_cache_hit_ ? 1 : 0;
        profile_.shadow_cache_misses = active_directional_shadow_.has_value() &&
                                               !directional_shadow_cache_hit_
                                           ? 1
                                           : 0;
        active_spot_shadow_ = ScheduleSpotShadow(input.lights, input.is_shadow_handle_valid);
        active_point_shadow_ = SchedulePointShadow(input.lights, input.is_shadow_handle_valid);
        spot_shadow_recorded_ = false;
        point_shadow_recorded_ = false;
        const auto ray_tracing_scene_prepare_started = std::chrono::steady_clock::now();
        const bool ray_tracing_scene_prepared = PrepareRayTracingScene();
        profile_.cpu_ray_tracing_scene_prepare_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - ray_tracing_scene_prepare_started)
                .count();
        if (!ray_tracing_scene_prepared)
        {
            result.normal_recording_completed = false;
            return result;
        }
        frame_ray_tracing_light_data_.clear();
        frame_ray_tracing_lighting_signature_ = 1469598103934665603ull;
        const auto add_lighting_signature = [this](float value) {
            frame_ray_tracing_lighting_signature_ ^=
                static_cast<uint64_t>(std::hash<float>{}(value));
            frame_ray_tracing_lighting_signature_ *= 1099511628211ull;
        };
        constexpr std::size_t kPathTraceMaximumLights = 128;
        if (path_tracing_enabled_)
        {
            for (const Light &light : input.lights)
            {
                if (!light.desc.enabled || frame_ray_tracing_light_data_.size() >=
                                              kPathTraceMaximumLights)
                {
                    continue;
                }
                RayTracingPathLightData record{};
                record.color_intensity = Vector4f{light.desc.color, light.desc.intensity};
                record.parameters.z_ = light.desc.shadow.has_value() ? 1.0f : 0.0f;
                if (light.desc.type == LightType::Directional)
                {
                    const auto *directional =
                        std::get_if<DirectionalLightData>(&light.desc.type_data);
                    if (directional == nullptr || directional->direction.SquareLength() <= 0.0f)
                    {
                        continue;
                    }
                    record.position_or_type.w_ = 0.0f;
                    record.direction_and_range =
                        Vector4f{directional->direction.GetSafetyNormalize(), 0.0f};
                }
                else if (light.desc.type == LightType::Point)
                {
                    const auto *point = std::get_if<PointLightData>(&light.desc.type_data);
                    if (point == nullptr || !(point->range > 0.0f))
                    {
                        continue;
                    }
                    record.position_or_type = Vector4f{point->position, 1.0f};
                    record.direction_and_range.w_ = point->range;
                }
                else
                {
                    const auto *spot = std::get_if<SpotLightData>(&light.desc.type_data);
                    if (spot == nullptr || !(spot->range > 0.0f) ||
                        spot->direction.SquareLength() <= 0.0f)
                    {
                        continue;
                    }
                    record.position_or_type = Vector4f{spot->position, 2.0f};
                    record.direction_and_range =
                        Vector4f{spot->direction.GetSafetyNormalize(), spot->range};
                    record.parameters.x_ = std::cos(spot->inner_cone_radians);
                    record.parameters.y_ = std::cos(spot->outer_cone_radians);
                }
                for (const float component : {record.position_or_type.x_, record.position_or_type.y_,
                                              record.position_or_type.z_, record.position_or_type.w_,
                                              record.direction_and_range.x_, record.direction_and_range.y_,
                                              record.direction_and_range.z_, record.direction_and_range.w_,
                                              record.color_intensity.x_, record.color_intensity.y_,
                                              record.color_intensity.z_, record.color_intensity.w_,
                                              record.parameters.x_, record.parameters.y_,
                                              record.parameters.z_, record.parameters.w_})
                {
                    add_lighting_signature(component);
                }
                frame_ray_tracing_light_data_.push_back(record);
            }
        }
        const bool ray_query_shadow =
            backend_->GetCapabilities().SupportsRayQueryShadows() &&
            backend_->GetActiveTopLevelAccelerationStructure().IsValid() &&
            !frame_ray_tracing_instances_.empty();
        // The first loading frame can precede the first populated world
        // snapshot. Do not select an RT graph variant until its imported TLAS
        // provider exists; the next frame will rebuild the plan selection.
        const bool path_trace_scene_within_capacity =
            frame_ray_tracing_geometries_.size() <= kPathTraceMaximumSceneRecords &&
            frame_ray_tracing_instance_data_.size() <= kPathTraceMaximumSceneRecords &&
            frame_ray_tracing_material_data_.size() <= kPathTraceMaximumSceneRecords &&
            frame_ray_tracing_light_data_.size() <= 128;
        const bool has_path_trace_scene = !frame_ray_tracing_instances_.empty();
        const bool has_path_trace_tlas = ray_tracing_tlas_.IsValid();
        if (ray_tracing_path_tracing_available_ &&
            (!has_path_trace_scene || !has_path_trace_tlas || !path_trace_scene_within_capacity))
        {
            const uint64_t diagnostic_signature =
                frame_ray_tracing_instance_signature_ ^
                (static_cast<uint64_t>(frame_ray_tracing_geometries_.size()) << 32u) ^
                static_cast<uint64_t>(frame_ray_tracing_instance_data_.size()) ^ 1u;
            if (path_trace_scene_limit_diagnostic_signature_ != diagnostic_signature)
            {
                KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                       "Path tracing inactive: geometry_records=%zu instance_records=%zu "
                       "material_records=%zu light_records=%zu "
                       "TLAS_valid=%s (limit=%zu)",
                       frame_ray_tracing_geometries_.size(),
                       frame_ray_tracing_instance_data_.size(),
                       frame_ray_tracing_material_data_.size(),
                       frame_ray_tracing_light_data_.size(),
                       has_path_trace_tlas ? "true" : "false",
                       kPathTraceMaximumSceneRecords);
                path_trace_scene_limit_diagnostic_signature_ = diagnostic_signature;
            }
        }
        else
        {
            path_trace_scene_limit_diagnostic_signature_ = 0;
        }
        const bool ray_tracing_path_trace = path_tracing_enabled_ &&
                                            ray_tracing_path_tracing_available_ &&
                                            ray_tracing_tlas_.IsValid() &&
                                            has_path_trace_scene &&
                                            path_trace_scene_within_capacity;
        active_ray_tracing_path_trace_ = ray_tracing_path_trace;
        profile_.path_trace_active = ray_tracing_path_trace;
        if (ray_tracing_path_trace)
        {
            if (!PrepareToneMapPassResources())
            {
                result.normal_recording_completed = false;
                return result;
            }
            const graphics::Extent2D extent = frame_context.GetRenderExtent();
            if (!EnsurePathTraceHistoryTargets(extent.width, extent.height))
            {
                result.normal_recording_completed = false;
                return result;
            }
            const uint64_t signature = PathTraceHistorySignature(extent.width, extent.height);
            if (signature != path_trace_history_signature_)
            {
                path_trace_sample_count_ = 0;
                path_trace_history_signature_ = signature;
            }
            profile_.path_trace_samples = path_trace_sample_count_;
        }
        const CompiledRenderGraph *const frame_plan =
            GetFramePlan(RenderFrameConditions{is_deferred_capture, ray_query_shadow,
                                               frame_ray_tracing_blas_build_,
                                               frame_ray_tracing_tlas_build_,
                                               ray_tracing_path_trace});
        if (frame_plan == nullptr)
        {
            result.normal_recording_completed = false;
            return result;
        }
        if (!AcquireFrameTransients(*frame_plan))
        {
            // Deferred lighting writes it and tone map reads it, so a frame
            // without it cannot record.
            result.normal_recording_completed = false;
            return result;
        }
        if (!BuildFrameResourceBindings(*frame_plan))
        {
            result.normal_recording_completed = false;
            return result;
        }
        active_pass_frame_.emplace(*frame_plan);
        active_frame_plan_ = frame_plan;
        frame_execution_failed_ = false;
        const auto graph_execute_started = std::chrono::steady_clock::now();
        const bool cursor_started = active_pass_frame_->ExecuteRenderer(
            [this, &input, frame_plan](const CompiledRenderGraph::Pass &pass) {
                // The authored declaration always keys its passes; an unkeyed
                // pass cannot be dispatched and must not be reported as a
                // successful visit.
                if (!pass.user_key.has_value())
                {
                    return false;
                }
                const auto pass_id = static_cast<FixedRenderPassId>(*pass.user_key);
                // A shadow cache hit keeps the previous frame's depth map. The
                // pass must not open its target, because opening it clears the
                // very contents the cache exists to preserve, so the skip is
                // decided here, before the boundary.
                if (pass_id == FixedRenderPassId::DirectionalShadow &&
                    directional_shadow_cache_hit_)
                {
                    return true;
                }
                if (!ApplyPassTransitions(*frame_plan, pass))
                {
                    return false;
                }
                // The executor owns the attachment boundary now: the pass's write
                // use names the target it records into, so no pass opens or
                // closes its own target.
                RenderTarget *const attachment = pass_id == FixedRenderPassId::RayTracingPathTrace
                                                     ? nullptr
                                                     : ResolvePassAttachment(pass);
                const bool has_attachment_write = std::any_of(
                    pass.uses.begin(), pass.uses.end(), [](const RenderGraphResourceUse &use) {
                        return use.access == RenderGraphAccess::Write &&
                               std::holds_alternative<GraphTextureHandle>(use.handle);
                    });
                graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
                if ((has_attachment_write && attachment == nullptr &&
                     pass_id != FixedRenderPassId::RayTracingPathTrace) ||
                    (attachment != nullptr && (recorder == nullptr ||
                                               !attachment->BeginRecording(*recorder))))
                {
                    return false;
                }
                const bool succeeded = ExecutePass(pass_id, input.lights);
                if (attachment != nullptr)
                {
                    attachment->EndRecording(*recorder);
                }
                return succeeded;
            });
        profile_.cpu_graph_execute_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - graph_execute_started)
                .count();
        result.normal_recording_completed =
            cursor_started && !active_pass_frame_->HasRequiredFailure();
        if (input.pending_capture.has_value())
        {
            result.capture_target_ready =
                input.pending_capture.value() == CaptureView::SceneColor ||
                active_pass_frame_->GetOutcome(
                    static_cast<uint64_t>(FixedRenderPassId::CaptureView)) ==
                    RenderGraphPassOutcome::Executed;
        }
        const MaterialProfileCounters material_profile =
            material_system_->GetProfileCounters();
        const FrameContextProfileCounters frame_profile =
            frame_context.GetProfileCounters();
        profile_.cpu_material_resolution_ms += material_profile.resolution_cpu_ms;
        profile_.material_resolution_calls += material_profile.resolution_calls;
        profile_.cpu_material_resolution_ms += frame_profile.material_resolution_cpu_ms;
        profile_.cpu_uniform_write_ms += frame_profile.uniform_write_cpu_ms;
        profile_.uniform_writes += frame_profile.uniform_writes;
        profile_.uniform_write_bytes += frame_profile.uniform_write_bytes;
        frame_lighting_binding_ = {};
        return result;
    }

    bool DeferredRenderer::ExecutePass(FixedRenderPassId id, const std::vector<Light> &lights)
    {
        const size_t profile_index = static_cast<size_t>(id);
        const auto started = std::chrono::steady_clock::now();
        active_profile_pass_ = profile_index;
        backend_->BeginGpuProfilePass(static_cast<uint32_t>(profile_index));
        bool succeeded = false;
        switch (id)
        {
        case FixedRenderPassId::DirectionalShadow:
            succeeded = RecordDirectionalShadowPass();
            break;
        case FixedRenderPassId::SpotShadow:
            succeeded = RecordSpotShadowPass();
            break;
        case FixedRenderPassId::PointShadow:
            succeeded = RecordPointShadowPass();
            break;
        case FixedRenderPassId::GBuffer:
            succeeded = RecordGBufferPass();
            break;
        case FixedRenderPassId::DeferredLighting:
        {
            ResolvedLightShadowBindings resolved_shadows;
            if (active_directional_shadow_.has_value())
            {
                const DirectionalShadowFrame &shadow = *active_directional_shadow_;
                resolved_shadows.push_back({shadow.job.source_light, shadow.shadow,
                                            shadow.job.kind, shadow.job.binding_slot});
            }
            if (active_spot_shadow_.has_value() && spot_shadow_recorded_)
            {
                const SpotShadowFrame &shadow = *active_spot_shadow_;
                resolved_shadows.push_back({shadow.job.source_light, shadow.shadow,
                                            shadow.job.kind, shadow.job.binding_slot});
            }
            if (active_point_shadow_.has_value() && point_shadow_recorded_)
            {
                const PointShadowFrame &shadow = *active_point_shadow_;
                resolved_shadows.push_back({shadow.job.source_light, shadow.shadow,
                                            shadow.job.kind, shadow.job.binding_slot});
            }
            frame_lighting_binding_ = active_frame_context_->CreateLightingBinding(
                BuildLightGpuFrameData(lights, resolved_shadows));
            succeeded = RecordDeferredLightingPass();
            break;
        }
        case FixedRenderPassId::ToneMap:
            succeeded = RecordToneMapPass();
            break;
        case FixedRenderPassId::RayTracingToneMap:
            succeeded = RecordToneMapPass();
            break;
        case FixedRenderPassId::RayTracingPathTrace:
            succeeded = RecordRayTracingPathTracePass();
            break;
        case FixedRenderPassId::CaptureView:
            succeeded = active_pending_capture_.has_value() &&
                        RecordCaptureViewPass(*active_pending_capture_);
            break;
        case FixedRenderPassId::EditorComposite:
            succeeded = false;
            break;
        case FixedRenderPassId::RayTracingBlasBuild:
            succeeded = RecordRayTracingBlasBuild();
            break;
        case FixedRenderPassId::RayTracingTlasBuild:
            succeeded = RecordRayTracingTlasBuild();
            break;
        case FixedRenderPassId::Count:
            succeeded = false;
            break;
        }
        backend_->EndGpuProfilePass(static_cast<uint32_t>(profile_index));
        profile_.passes[profile_index].cpu_time_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        active_profile_pass_.reset();
        return succeeded;
    }

    bool DeferredRenderer::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!active_pass_frame_.has_value())
        {
            return false;
        }
        const auto started = std::chrono::steady_clock::now();
        backend_->BeginGpuProfilePass(static_cast<uint32_t>(RenderProfilePass::EditorComposite));
        // The external terminal samples SceneColor, and the host's callback below
        // is what reads it, so its requirement is applied before the callback
        // rather than after the sweep.
        if (active_frame_plan_ != nullptr)
        {
            for (const CompiledRenderGraph::Pass &pass : active_frame_plan_->Passes())
            {
                if (pass.owner == RenderGraphPassOwner::External && pass.terminal)
                {
                    if (!ApplyPassTransitions(*active_frame_plan_, pass))
                    {
                        frame_execution_failed_ = true;
                        return false;
                    }
                    break;
                }
            }
        }
        const bool succeeded = active_pass_frame_->ExecuteExternal(record_pass);
        backend_->EndGpuProfilePass(static_cast<uint32_t>(RenderProfilePass::EditorComposite));
        profile_.passes[static_cast<size_t>(RenderProfilePass::EditorComposite)].cpu_time_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        return succeeded;
    }

    bool DeferredRenderer::FinalizeFrame()
    {
        if (!active_pass_frame_.has_value())
        {
            return false;
        }
        std::string error;
        const bool finalized = active_pass_frame_->Finalize(error);
        if (!finalized && !error.empty())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Fixed render pass finalization failed: %s",
                   error.c_str());
        }
        if (frame_execution_failed_)
        {
            if (error.empty())
            {
                error = "Render graph external pass requirements failed.";
            }
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", error.c_str());
        }
        const bool required_pass_failed = active_pass_frame_->HasRequiredFailure();
        const bool succeeded = finalized && !frame_execution_failed_ &&
                               !required_pass_failed;
        const detail::PathTraceHistoryProgress history_progress =
            detail::CommitPathTraceHistoryProgress(
                {path_trace_sample_count_, path_trace_write_index_}, finalized,
                frame_execution_failed_, required_pass_failed,
                active_ray_tracing_path_trace_, kPathTraceSamplesPerDispatch);
        path_trace_sample_count_ = history_progress.sample_count;
        path_trace_write_index_ = history_progress.write_index;
        if (succeeded && active_ray_tracing_path_trace_)
        {
            profile_.path_trace_samples = path_trace_sample_count_;
        }
        if (finalized)
        {
            // Released after the sweep, so the next frame takes the same
            // instance back rather than a second one: the caller's descriptor
            // sets are keyed on this target's handles.
            ReleaseFrameTransients();
            active_pass_frame_.reset();
            active_frame_plan_ = nullptr;
            active_pending_capture_.reset();
            active_frame_context_ = nullptr;
            render_world_ = nullptr;
            frame_lighting_binding_ = {};
        }
        return succeeded;
    }

    void DeferredRenderer::UpdateEnvironment(const RenderSceneFrameInput &input)
    {
        const std::optional<EnvironmentSourceDesc> &source = input.environment;
        const std::optional<EnvironmentSourceHandle> &source_handle = input.environment_handle;
        if (!source.has_value() || !source_handle.has_value())
        {
            failed_environment_source_.reset();
            active_environment_ = {};
            return;
        }
        if (level_environment_.source_asset == source->texture_asset &&
            level_environment_.HasCompleteBindings())
        {
            level_environment_.ibl_intensity = source->ibl_intensity;
            active_environment_ = level_environment_;
            failed_environment_source_.reset();
            return;
        }
        EnvironmentBindingBundle candidate{};
        if (ResolveLevelEnvironment(*source, candidate))
        {
            level_environment_ = candidate;
            active_environment_ = std::move(candidate);
            failed_environment_source_.reset();
            return;
        }
        if (!failed_environment_source_.has_value() ||
            !(*failed_environment_source_ == *source_handle))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Level environment source could not be resolved; retaining baseline");
            failed_environment_source_ = source_handle;
        }
        active_environment_ = {};
    }

    bool DeferredRenderer::EnsurePathTraceHistoryTargets(uint32_t width, uint32_t height)
    {
        if (!backend_ || width == 0 || height == 0)
            return false;
        if (path_trace_history_targets_[0] && path_trace_history_targets_[1] &&
            path_trace_history_targets_[0]->IsValid() &&
            path_trace_history_targets_[1]->IsValid() &&
            path_trace_history_targets_[0]->GetWidth() == width &&
            path_trace_history_targets_[0]->GetHeight() == height)
            return true;

        if (path_trace_history_targets_[0] || path_trace_history_targets_[1])
        {
            if (graphics::RayTracingResourceOwner *const owner =
                    backend_->GetRayTracingResourceOwner();
                owner && ray_tracing_path_tracing_bindings_.IsValid())
            {
                owner->DestroyRayTracingResourceBindingSet(
                    ray_tracing_path_tracing_bindings_);
                ray_tracing_path_tracing_bindings_ = {};
            }
            backend_->WaitIdle();
        }
        const graphics::RenderTargetDesc desc =
            RendererFrameTargets::DescribeSceneHdr(width, height);
        for (auto &target : path_trace_history_targets_)
        {
            if (!target) target = std::make_unique<RenderTarget>();
            target->Initialize(*backend_, desc);
            if (!target->IsValid()) return false;
        }
        path_trace_sample_count_ = 0;
        path_trace_write_index_ = 0;
        path_trace_history_signature_ = 0;
        return true;
    }

    uint64_t DeferredRenderer::PathTraceHistorySignature(uint32_t width,
                                                          uint32_t height) const
    {
        detail::PathTraceHistorySignatureInput input{};
        input.width = width;
        input.height = height;
        input.scene_signature = frame_ray_tracing_instance_signature_;
        input.geometry_count = frame_ray_tracing_geometries_.size();
        input.material_signature = frame_ray_tracing_material_signature_;
        input.lighting_signature = frame_ray_tracing_lighting_signature_;
        if (active_environment_.HasCompleteBindings())
        {
            input.lighting_signature ^= active_environment_.source_asset.Pack();
            input.lighting_signature *= 1099511628211ull;
            input.lighting_signature ^= static_cast<uint64_t>(
                std::hash<float>{}(active_environment_.ibl_intensity));
        }
        input.pipeline_id = ray_tracing_path_tracing_pipeline_.id;
        input.pipeline_generation = ray_tracing_path_tracing_pipeline_.generation;
        input.shader_signature = path_trace_shader_signature_;
        input.output_pipeline_id = tone_map_pipeline_.id;
        input.output_pipeline_generation = tone_map_pipeline_.generation;
        input.output_shader_signature = tone_map_shader_signature_;
        input.probe_mode = static_cast<uint32_t>(path_trace_probe_mode_);
        input.exposure = kPathTraceExposure;
        input.tone_map_operator = kPathTraceToneMapOperatorReinhard;
        input.output_transfer = kPathTraceOutputTransferSrgb;
        input.light_parameters = {};
        input.ray_parameters = {
            kPathTraceRayMinimumDistance, kPathTraceRayMaximumDistance,
            kPathTraceSecondaryRayOffset, 0.0f};
        input.integrator_parameters = {
            kPathTraceSamplesPerDispatch, kPathTraceDirectLightSamples,
            kPathTraceDiffuseBounces, kPathTraceIntegratorVersion};
        input.rng_seed = kPathTraceRngSeed;
        input.rng_policy_version = kPathTraceRngPolicyVersion;
        const Matrix4f view_projection = scene_camera_.GetViewProjectionMatrix();
        std::size_t value_index = 0;
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                input.view_projection[value_index++] = view_projection[row][column];
        const Vector3f position = scene_camera_.GetPosition();
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            input.camera_position[axis] = position[axis];
        }
        return detail::ComputePathTraceHistorySignature(input);
    }

    RenderTarget *DeferredRenderer::ResolveNamedFrameTarget(std::string_view name)
    {
        if (name == "SceneHdr")
        {
            return active_ray_tracing_path_trace_
                       ? path_trace_history_targets_[path_trace_write_index_].get()
                       : transient_scene_hdr_.get();
        }
        if (name == "PathTraceHistory")
        {
            return path_trace_history_targets_[1u - path_trace_write_index_].get();
        }
        if (name == "SceneColor")
        {
            return frame_targets_.GetTarget(RenderTargetName::SceneColor);
        }
        if (name == "GBuffer")
        {
            return frame_targets_.GetTarget(RenderTargetName::GBuffer);
        }
        if (name == "DirectionalShadow")
        {
            return frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
        }
        if (name == "SpotShadow")
        {
            return frame_targets_.GetTarget(RenderTargetName::SpotShadow);
        }
        if (name == "PointShadow")
        {
            return frame_targets_.GetTarget(RenderTargetName::PointShadow);
        }
        if (name == "CaptureOutput")
        {
            return frame_targets_.GetTarget(RenderTargetName::CaptureOutput);
        }
        return nullptr;
    }

    RenderTarget *DeferredRenderer::ResolveFrameTexture(GraphTextureHandle texture) const
    {
        const auto binding = std::find_if(
            frame_texture_bindings_.begin(), frame_texture_bindings_.end(),
            [texture](const FrameTextureBinding &entry) { return entry.logical == texture; });
        return binding != frame_texture_bindings_.end() ? binding->physical : nullptr;
    }

    RenderTarget *DeferredRenderer::ResolveFrameTextureByName(std::string_view name) const
    {
        const auto binding = std::find_if(
            frame_texture_bindings_.begin(), frame_texture_bindings_.end(),
            [name](const FrameTextureBinding &entry) { return entry.name == name; });
        return binding != frame_texture_bindings_.end() ? binding->physical : nullptr;
    }

    graphics::BufferHandle DeferredRenderer::ResolveFrameBuffer(GraphBufferHandle buffer) const
    {
        const auto binding = std::find_if(
            frame_buffer_bindings_.begin(), frame_buffer_bindings_.end(),
            [buffer](const FrameBufferBinding &entry) { return entry.logical == buffer; });
        return binding != frame_buffer_bindings_.end() ? binding->physical
                                                       : graphics::BufferHandle{};
    }

    graphics::AccelerationStructureHandle DeferredRenderer::ResolveFrameAccelerationStructure(
        GraphAccelerationStructureHandle acceleration_structure) const
    {
        const auto binding = std::find_if(
            frame_acceleration_structure_bindings_.begin(),
            frame_acceleration_structure_bindings_.end(),
            [acceleration_structure](const FrameAccelerationStructureBinding &entry) {
                return entry.logical == acceleration_structure;
            });
        return binding != frame_acceleration_structure_bindings_.end()
                   ? binding->physical
                   : graphics::AccelerationStructureHandle{};
    }

    bool DeferredRenderer::BuildFrameResourceBindings(const CompiledRenderGraph &plan)
    {
        frame_texture_bindings_.clear();
        frame_buffer_bindings_.clear();
        frame_acceleration_structure_bindings_.clear();
        for (const RenderGraphLifetimeInterval &lifetime : plan.Lifetimes())
        {
            if (const auto *texture = std::get_if<GraphTextureHandle>(&lifetime.handle))
            {
                RenderTarget *const target = ResolveNamedFrameTarget(lifetime.resource_name);
                if (target == nullptr)
                {
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                           "No frame binding for graph texture '%s'", lifetime.resource_name.c_str());
                    frame_texture_bindings_.clear();
                    return false;
                }
                frame_texture_bindings_.push_back({*texture, lifetime.resource_name, target});
                continue;
            }

            if (const auto *buffer = std::get_if<GraphBufferHandle>(&lifetime.handle))
            {
                if (IsRayTracingVirtualBuffer(lifetime.resource_name))
                {
                    // Geometry buffers are a logical group resolved by the
                    // backend provider during transition application. Instance
                    // and scratch storage stay Graphics-owned inside the AS
                    // owner and intentionally have no Render-visible handle.
                    if (lifetime.resource_name == "SceneGeometry" &&
                        !frame_ray_tracing_geometries_.empty())
                    {
                        frame_buffer_bindings_.push_back(
                            {*buffer, lifetime.resource_name,
                             frame_ray_tracing_geometries_.front().vertex_buffer});
                    }
                    continue;
                }
                // R4.1 makes the missing physical-buffer binding explicit for
                // ordinary graph buffers; the RT build provider is the only
                // supported exception in this frame plan.
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "No frame binding provider for graph buffer '%s'",
                       lifetime.resource_name.c_str());
                frame_texture_bindings_.clear();
                frame_buffer_bindings_.clear();
                frame_acceleration_structure_bindings_.clear();
                return false;
            }

            const auto *acceleration_structure =
                std::get_if<GraphAccelerationStructureHandle>(&lifetime.handle);
            if (lifetime.resource_name == "SceneBLAS")
            {
                if (frame_ray_tracing_mesh_builds_.empty())
                {
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                           "No BLAS provider for graph resource '%s'",
                           lifetime.resource_name.c_str());
                    frame_texture_bindings_.clear();
                    frame_buffer_bindings_.clear();
                    frame_acceleration_structure_bindings_.clear();
                    return false;
                }
                continue;
            }
            // The graph is bound before the TLAS build pass records. Use the
            // renderer-owned handle prepared for this frame; the Vulkan owner
            // marks it active when the build command is recorded.
            const graphics::AccelerationStructureHandle physical =
                ray_tracing_tlas_.IsValid()
                    ? ray_tracing_tlas_
                    : (backend_ != nullptr
                           ? backend_->GetActiveTopLevelAccelerationStructure()
                           : graphics::AccelerationStructureHandle{});
            if (acceleration_structure == nullptr || !physical.IsValid())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "No imported TLAS binding for graph resource '%s'",
                       lifetime.resource_name.c_str());
                frame_texture_bindings_.clear();
                frame_buffer_bindings_.clear();
                frame_acceleration_structure_bindings_.clear();
                return false;
            }
            frame_acceleration_structure_bindings_.push_back(
                {*acceleration_structure, lifetime.resource_name, physical});
        }
        return true;
    }

    bool DeferredRenderer::ValidatePassBindings(const CompiledRenderGraph::Pass &pass) const
    {
        for (const RenderGraphResourceUse &use : pass.uses)
        {
            if (const auto *texture = std::get_if<GraphTextureHandle>(&use.handle))
            {
                if (ResolveFrameTexture(*texture) == nullptr)
                {
                    return false;
                }
            }
            else if (const auto *buffer = std::get_if<GraphBufferHandle>(&use.handle))
            {
                std::string_view resource_name;
                if (active_frame_plan_ != nullptr)
                {
                    for (const RenderGraphLifetimeInterval &lifetime :
                         active_frame_plan_->Lifetimes())
                    {
                        const auto *lifetime_buffer =
                            std::get_if<GraphBufferHandle>(&lifetime.handle);
                        if (lifetime_buffer != nullptr && *lifetime_buffer == *buffer)
                        {
                            resource_name = lifetime.resource_name;
                            break;
                        }
                    }
                }
                if (IsRayTracingVirtualBuffer(resource_name))
                {
                    continue;
                }
                if (!ResolveFrameBuffer(*buffer).IsValid())
                {
                    return false;
                }
            }
            else
            {
                const auto acceleration_structure =
                    std::get<GraphAccelerationStructureHandle>(use.handle);
                std::string_view resource_name;
                if (active_frame_plan_ != nullptr)
                {
                    for (const RenderGraphLifetimeInterval &lifetime :
                         active_frame_plan_->Lifetimes())
                    {
                        const auto *lifetime_acceleration_structure =
                            std::get_if<GraphAccelerationStructureHandle>(&lifetime.handle);
                        if (lifetime_acceleration_structure != nullptr &&
                            *lifetime_acceleration_structure == acceleration_structure)
                        {
                            resource_name = lifetime.resource_name;
                            break;
                        }
                    }
                }
                if (resource_name == "SceneBLAS")
                {
                    if (frame_ray_tracing_mesh_builds_.empty())
                    {
                        return false;
                    }
                    continue;
                }
                if (!ResolveFrameAccelerationStructure(acceleration_structure).IsValid())
                {
                    return false;
                }
            }
        }
        return true;
    }

    RenderTarget *DeferredRenderer::ResolvePassAttachment(const CompiledRenderGraph::Pass &pass)
    {
        // A renderer pass records into exactly one target, named by its write
        // use. A pass with no write use records no attachment.
        for (const RenderGraphResourceUse &use : pass.uses)
        {
            if (use.access != RenderGraphAccess::Write)
            {
                continue;
            }
            if (const auto *texture = std::get_if<GraphTextureHandle>(&use.handle))
            {
                return ResolveFrameTexture(*texture);
            }
        }
        return nullptr;
    }

    std::optional<graphics::RenderTargetDesc> DeferredRenderer::DescribeFrameTransient(
        uint64_t key, const graphics::Extent2D &extent) const
    {
        if (key == static_cast<uint64_t>(RenderFrameTransient::SceneHdr))
        {
            return RendererFrameTargets::DescribeSceneHdr(extent.width, extent.height);
        }
        return std::nullopt;
    }

    bool DeferredRenderer::AcquireFrameTransients(const CompiledRenderGraph &plan)
    {
        if (backend_ == nullptr || !active_frame_context_)
        {
            return false;
        }
        const graphics::Extent2D extent = active_frame_context_->GetRenderExtent();
        for (const CompiledRenderGraph::TransientResource &transient : plan.Transients())
        {
            const std::optional<graphics::RenderTargetDesc> desc =
                DescribeFrameTransient(transient.key, extent);
            if (!desc.has_value())
            {
                // A declared transient this renderer cannot describe is a
                // declaration it does not implement, and silently skipping it
                // would leave a pass reading nothing.
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "No description for declared transient '%s'", transient.name.c_str());
                return false;
            }
            const graphics::RenderTargetHandle handle =
                backend_->AcquireTransientRenderTarget(*desc);
            if (!handle.IsValid())
            {
                return false;
            }
            if (!transient_scene_hdr_)
            {
                transient_scene_hdr_ = std::make_unique<RenderTarget>();
            }
            // Adopt, so dropping the wrapper never destroys what the pool owns.
            transient_scene_hdr_->Adopt(*backend_, handle, *desc);
            if (!transient_scene_hdr_->IsValid())
            {
                return false;
            }
        }
        return true;
    }

    void DeferredRenderer::ReleaseFrameTransients()
    {
        if (!transient_scene_hdr_ || backend_ == nullptr)
        {
            frame_texture_bindings_.clear();
            frame_buffer_bindings_.clear();
            frame_acceleration_structure_bindings_.clear();
            return;
        }
        const graphics::RenderTargetHandle handle = transient_scene_hdr_->GetHandle();
        transient_scene_hdr_->Cleanup();
        if (handle.IsValid())
            backend_->ReleaseTransientRenderTarget(handle);
        frame_texture_bindings_.clear();
        frame_buffer_bindings_.clear();
        frame_acceleration_structure_bindings_.clear();
    }

    bool DeferredRenderer::ApplyPassTransitions(const CompiledRenderGraph &plan,
                                                const CompiledRenderGraph::Pass &pass)
    {
        graphics::CommandRecorder *const recorder =
            backend_ != nullptr ? backend_->GetCommandRecorder() : nullptr;
        if (recorder == nullptr || !ValidatePassBindings(pass))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Render graph pass '%s' has an unresolved physical binding",
                   pass.name.c_str());
            return false;
        }
        if (pass.transition_count == 0)
        {
            return true;
        }
        const std::vector<RenderGraphTransitionIntent> &transitions = plan.Transitions();
        for (std::size_t index = 0; index < pass.transition_count; ++index)
        {
            const std::size_t intent_index = pass.transition_offset + index;
            if (intent_index >= transitions.size())
            {
                break;
            }
            const RenderGraphTransitionIntent &intent = transitions[intent_index];
            if (const auto *texture = std::get_if<GraphTextureHandle>(&intent.handle))
            {
                RenderTarget *const target = ResolveFrameTexture(*texture);
                if (target == nullptr ||
                    !recorder->RequireRenderTargetUsage(
                        target->GetHandle(), ToResourceUsage(intent.usage),
                        ToAttachmentScope(intent.scope)))
                {
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                           "Render graph texture requirement failed for pass '%s'",
                           pass.name.c_str());
                    return false;
                }
                continue;
            }
            if (const auto *buffer = std::get_if<GraphBufferHandle>(&intent.handle))
            {
                if (IsRayTracingVirtualBuffer(intent.resource_name))
                {
                    if (intent.resource_name == "SceneGeometry")
                    {
                        for (const graphics::RayTracingGeometryDesc &geometry :
                             frame_ray_tracing_geometries_)
                        {
                            for (const graphics::BufferHandle physical :
                                 {geometry.vertex_buffer, geometry.index_buffer})
                            {
                                if (!physical.IsValid() ||
                                    !recorder->RequireBufferUsage(
                                        physical, ToResourceUsage(intent.usage)))
                                {
                                    KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                                           "Render graph geometry requirement failed for pass '%s'",
                                           pass.name.c_str());
                                    return false;
                                }
                            }
                        }
                    }
                    continue;
                }
                const graphics::BufferHandle physical = ResolveFrameBuffer(*buffer);
                if (!physical.IsValid() ||
                    !recorder->RequireBufferUsage(physical, ToResourceUsage(intent.usage)))
                {
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                           "Render graph buffer requirement failed for pass '%s'",
                           pass.name.c_str());
                    return false;
                }
                continue;
            }
            const auto *acceleration_structure =
                std::get_if<GraphAccelerationStructureHandle>(&intent.handle);
            if (intent.resource_name == "SceneBLAS")
            {
                for (const RayTracingMeshBuild &mesh_build : frame_ray_tracing_mesh_builds_)
                {
                    if (!mesh_build.blas.IsValid() ||
                        !recorder->RequireAccelerationStructureUsage(
                            mesh_build.blas, ToResourceUsage(intent.usage)))
                    {
                        KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                               "Render graph BLAS requirement failed for pass '%s'",
                               pass.name.c_str());
                        return false;
                    }
                }
                continue;
            }
            const graphics::AccelerationStructureHandle physical =
                acceleration_structure != nullptr
                    ? ResolveFrameAccelerationStructure(*acceleration_structure)
                    : graphics::AccelerationStructureHandle{};
            if (!physical.IsValid() ||
                !recorder->RequireAccelerationStructureUsage(
                    physical, ToResourceUsage(intent.usage)))
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Render graph acceleration structure requirement failed for pass '%s'",
                       pass.name.c_str());
                return false;
            }
        }
        return true;
    }

    void DeferredRenderer::ConfigureFramePlans()
    {
        // The declaration is static, so each condition variant is compiled once
        // here and reused for every frame that selects it.
        frame_plan_valid_ = true;
        frame_plan_compile_ms_ = 0.0;
        for (uint32_t condition_bits = 0; condition_bits < frame_plans_.size();
             ++condition_bits)
        {
            const RenderFrameConditions conditions{
                (condition_bits & 1U) != 0,
                (condition_bits & 2U) != 0,
                (condition_bits & 4U) != 0,
                (condition_bits & 8U) != 0,
                (condition_bits & 16U) != 0};
            const std::size_t slot =
                (conditions.diagnostic_capture ? 1U : 0U) |
                (conditions.ray_query_shadow ? 2U : 0U) |
                (conditions.ray_tracing_blas_build ? 4U : 0U) |
                (conditions.ray_tracing_tlas_build ? 8U : 0U) |
                (conditions.ray_tracing_path_trace ? 16U : 0U);
            const auto started = std::chrono::steady_clock::now();
            frame_plans_[slot] = CompileRenderFrameGraph(conditions);
            frame_plan_compile_ms_ += std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now() - started)
                                          .count();
            if (frame_plans_[slot]->graph.has_value())
            {
                continue;
            }
            frame_plan_valid_ = false;
            for (const RenderGraphDiagnostic &diagnostic : frame_plans_[slot]->diagnostics)
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Render graph declaration is invalid: %s",
                       diagnostic.message.c_str());
            }
        }
    }

    const CompiledRenderGraph *DeferredRenderer::GetFramePlan(
        RenderFrameConditions conditions) const
    {
        const std::optional<RenderGraphCompileResult> &plan =
            frame_plans_[(conditions.diagnostic_capture ? 1U : 0U) |
                        (conditions.ray_query_shadow ? 2U : 0U) |
                        (conditions.ray_tracing_blas_build ? 4U : 0U) |
                        (conditions.ray_tracing_tlas_build ? 8U : 0U) |
                        (conditions.ray_tracing_path_trace ? 16U : 0U)];
        if (!plan.has_value() || !plan->graph.has_value())
        {
            return nullptr;
        }
        return &*plan->graph;
    }

    std::optional<DeferredRenderer::DirectionalShadowFrame> DeferredRenderer::ScheduleDirectionalShadow(
        const std::vector<Light> &lights,
        const std::function<bool(ShadowHandle)> &is_shadow_handle_valid)
    {
        // The first implementation schedules at most one requested directional
        // map. The source's private ShadowHandle is the opt-in; absent, disabled,
        // or malformed records intentionally remain unshadowed.
        constexpr uint32_t kDirectionalShadowResolution = 2048;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Directional ||
                !light.desc.shadow.has_value() ||
                !is_shadow_handle_valid || !is_shadow_handle_valid(*light.desc.shadow) ||
                !IsShadowKindCompatible(light.desc.type, ShadowKind::Directional2D))
            {
                continue;
            }
            const auto *const directional = std::get_if<DirectionalLightData>(&light.desc.type_data);
            if (!directional || directional->direction.SquareLength() <= 0.0f)
            {
                continue;
            }

            const Vector3f direction = directional->direction.GetSafetyNormalize();
            // Camera frustum culling applies only to GBufferPass. Shadow casters
            // come from the full render-world snapshot, so fit the directional
            // volume to their world bounds instead of a camera-derived box.
            const std::vector<VisibleMeshSection> &caster_candidates =
                BuildSectionCandidatesProfiled();
            DirectionalShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::Directional2D,
                         kDirectionalShadowResolution, 0};
            frame.shadow = *light.desc.shadow;
            frame.light_direction = direction;
            DirectionalShadowFit stamp_fit{};
            const std::optional<DirectionalShadowFit> effective_fit =
                BuildEffectiveDirectionalShadowFit(caster_candidates,
                                                    scene_camera_.GetPosition(), direction);
            if (effective_fit.has_value())
            {
                ++profile_.shadow_fit_evaluations;
                frame.view = effective_fit->matrices.view;
                frame.projection = effective_fit->matrices.projection;
                stamp_fit = *effective_fit;
            }
            else
            {
                // No ready draw can produce a shadow, but keep a valid clear-depth
                // frame binding until a caster is published.
                constexpr float kHalfExtent = 150.0f;
                constexpr float kDepthRange = 600.0f;
                const Vector3f center = scene_camera_.GetPosition();
                const Vector3f eye = center - direction * (kDepthRange * 0.5f);
                const Vector3f up = std::abs(direction.y_) > 0.98f
                                        ? Vector3f{0.0f, 0.0f, 1.0f}
                                        : Vector3f{0.0f, 1.0f, 0.0f};
                frame.view = Matrix4f::MakeCameraMatrix(eye, direction, up);
                frame.projection = Matrix4f::MakeOrthProjMatrix(
                    -kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent, 0.1f, kDepthRange);
                stamp_fit.matrices = {frame.view, frame.projection};
            }
            ++profile_.shadow_stamp_evaluations;
            frame.validity_stamp =
                ComputeDirectionalShadowStamp(light, caster_candidates, stamp_fit);
            return frame;
        }
        return std::nullopt;
    }

    std::optional<DeferredRenderer::SpotShadowFrame> DeferredRenderer::ScheduleSpotShadow(
        const std::vector<Light> &lights,
        const std::function<bool(ShadowHandle)> &is_shadow_handle_valid)
    {
        constexpr uint32_t kSpotShadowResolution = 1024;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Spot ||
                !light.desc.shadow.has_value() ||
                !is_shadow_handle_valid || !is_shadow_handle_valid(*light.desc.shadow) ||
                !IsShadowKindCompatible(light.desc.type, ShadowKind::Spot2D) ||
                !IsLightDescValid(light.desc))
            {
                continue;
            }
            const auto *const spot = std::get_if<SpotLightData>(&light.desc.type_data);
            if (spot == nullptr)
            {
                continue;
            }
            const Vector3f direction = spot->direction.GetSafetyNormalize();
            const float near_plane = std::min(std::max(0.01f, spot->range * 0.001f),
                                              spot->range * 0.5f);
            if (!(near_plane > 0.0f) || !(near_plane < spot->range))
            {
                continue;
            }
            const Vector3f up = std::abs(direction.y_) > 0.98f
                                    ? Vector3f{0.0f, 0.0f, 1.0f}
                                    : Vector3f{0.0f, 1.0f, 0.0f};
            SpotShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::Spot2D, kSpotShadowResolution, 1};
            frame.shadow = *light.desc.shadow;
            frame.position = spot->position;
            frame.light_direction = direction;
            frame.outer_cone_radians = spot->outer_cone_radians;
            frame.near_plane = near_plane;
            frame.far_plane = spot->range;
            frame.view = Matrix4f::MakeCameraMatrix(spot->position, direction, up);
            frame.projection = Matrix4f::MakePerProjMatrix(
                spot->outer_cone_radians * 2.0f, 1.0f, near_plane, spot->range);
            bool has_caster = false;
            const std::vector<VisibleMeshSection> &caster_candidates =
                BuildSectionCandidatesProfiled();
            for (const VisibleMeshSection &candidate : caster_candidates)
            {
                const MeshProxy &proxy = candidate.proxy;
                const std::optional<MaterialDrawClass> draw_class =
                    material_system_->GetDrawClass(proxy.material);
                if (proxy.flags.visible && proxy.flags.casts_shadow &&
                    IsSpotBoundsInsideFrustum(candidate.world_bounds, frame.view,
                                              frame.outer_cone_radians,
                                              frame.near_plane, frame.far_plane) &&
                    draw_class.has_value() && *draw_class == MaterialDrawClass::Opaque &&
                    material_system_->GetInstanceResolution(proxy.material).state ==
                        MaterialResourceState::Ready)
                {
                    has_caster = true;
                    break;
                }
            }
            if (!has_caster)
            {
                continue;
            }
            return frame;
        }
        return std::nullopt;
    }

    std::optional<DeferredRenderer::PointShadowFrame> DeferredRenderer::SchedulePointShadow(
        const std::vector<Light> &lights,
        const std::function<bool(ShadowHandle)> &is_shadow_handle_valid)
    {
        const std::vector<VisibleMeshSection> &proxies = BuildSectionCandidatesProfiled();
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Point ||
                !light.desc.shadow.has_value() ||
                !is_shadow_handle_valid || !is_shadow_handle_valid(*light.desc.shadow) ||
                !IsShadowKindCompatible(light.desc.type, ShadowKind::PointCube) ||
                !IsLightDescValid(light.desc))
            {
                continue;
            }
            const auto *const point = std::get_if<PointLightData>(&light.desc.type_data);
            if (point == nullptr)
            {
                continue;
            }
            const float near_plane = std::min(std::max(0.01f, point->range * 0.001f),
                                              point->range * 0.5f);
            if (!(near_plane > 0.0f) || !(near_plane < point->range))
            {
                continue;
            }
            PointShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::PointCube,
                         kPointShadowFaceResolution, 2};
            frame.shadow = *light.desc.shadow;
            frame.position = point->position;
            frame.near_plane = near_plane;
            frame.far_plane = point->range;
            bool has_caster = false;
            for (const VisibleMeshSection &candidate : proxies)
            {
                const MeshProxy &proxy = candidate.proxy;
                const std::optional<MaterialDrawClass> draw_class =
                    material_system_->GetDrawClass(proxy.material);
                const auto resolution = material_system_->GetInstanceResolution(proxy.material);
                if (proxy.flags.visible && proxy.flags.casts_shadow &&
                    IsBoundsInsideSphere(candidate.world_bounds, point->position, point->range) &&
                    draw_class.has_value() && *draw_class == MaterialDrawClass::Opaque &&
                    resolution.state == MaterialResourceState::Ready)
                {
                    has_caster = true;
                    break;
                }
            }
            if (!has_caster)
            {
                continue;
            }
            const auto &faces = GetPointShadowFaceTable();
            for (size_t face_index = 0; face_index < faces.size(); ++face_index)
            {
                const PointShadowFaceDesc &face = faces[face_index];
                const Matrix4f view = Matrix4f::MakeCameraMatrix(
                    point->position, face.direction, face.up);
                const Matrix4f projection = Matrix4f::MakePerProjMatrix(
                    1.570796327f, 1.0f, near_plane, point->range);
                frame.face_view_projections[face_index] = (projection * view).Transpose();
            }
            return frame;
        }
        return std::nullopt;
    }

    bool DeferredRenderer::RecordDirectionalShadowPass()
    {
        if (!active_frame_context_)
        {
            return false;
        }
        if (directional_shadow_cache_hit_)
        {
            return true;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
        if (!recorder || !shadow_target)
        {
            return false;
        }

        if (!active_directional_shadow_.has_value())
        {
            // Keep the always-bound fallback depth image in a valid sampled
            // layout when the directional fixture is intentionally disabled.
            directional_shadow_valid_ = false;
            return true;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            return false;
        }

        const DirectionalShadowFrame &shadow = *active_directional_shadow_;
        graphics::PerPassData per_pass_data{};
        per_pass_data.camera_data.view = shadow.view.Transpose();
        per_pass_data.camera_data.proj = shadow.projection.Transpose();
        const UniformAllocation per_pass = active_frame_context_->UpdateStableUniform(
            kDirectionalShadowPerPassUniformKey, per_pass_data);
        if (!per_pass.IsValid())
        {
            return false;
        }
        const std::vector<VisibleMeshSection> &shadow_caster_candidates =
            BuildSectionCandidatesProfiled();
        for (const VisibleMeshSection &candidate : shadow_caster_candidates)
        {
            const MeshProxy &proxy = candidate.proxy;
            const std::optional<MaterialDrawClass> draw_class =
                material_system_->GetDrawClass(proxy.material);
            if (proxy.flags.casts_shadow && draw_class.has_value() &&
                *draw_class == MaterialDrawClass::Opaque &&
                material_system_->GetInstanceResolution(proxy.material).state == MaterialResourceState::Ready)
            {
                RecordShadowCaster(proxy, per_pass, *recorder, candidate.section_index);
            }
        }
        directional_shadow_valid_ = true;
        directional_shadow_stamp_ = shadow.validity_stamp;
        return true;
    }

    bool DeferredRenderer::RecordSpotShadowPass()
    {
        if (!active_frame_context_)
        {
            return false;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::SpotShadow);
        if (!recorder || !shadow_target)
        {
            return false;
        }
        if (!active_spot_shadow_.has_value())
        {
            // Clear the fixed target even when the previous frame's selected
            // source was disabled, destroyed, stale, or over budget.
            return true;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            return false;
        }
        const SpotShadowFrame &shadow = *active_spot_shadow_;
        graphics::PerPassData per_pass_data{};
        per_pass_data.camera_data.view = shadow.view.Transpose();
        per_pass_data.camera_data.proj = shadow.projection.Transpose();
        const UniformAllocation per_pass = active_frame_context_->UpdateStableUniform(
            kSpotShadowPerPassUniformKey, per_pass_data);
        if (!per_pass.IsValid())
        {
            return false;
        }
        const std::vector<VisibleMeshSection> &shadow_caster_candidates =
            BuildSectionCandidatesProfiled();
        for (const VisibleMeshSection &candidate : shadow_caster_candidates)
        {
            const MeshProxy &proxy = candidate.proxy;
            const std::optional<MaterialDrawClass> draw_class =
                material_system_->GetDrawClass(proxy.material);
            if (!proxy.flags.visible || !proxy.flags.casts_shadow ||
                !IsSpotBoundsInsideFrustum(candidate.world_bounds, shadow.view,
                                           shadow.outer_cone_radians,
                                           shadow.near_plane, shadow.far_plane) ||
                !draw_class.has_value() || *draw_class != MaterialDrawClass::Opaque ||
                material_system_->GetInstanceResolution(proxy.material).state !=
                    MaterialResourceState::Ready)
            {
                continue;
            }
            RecordShadowCaster(proxy, per_pass, *recorder, candidate.section_index);
        }
        spot_shadow_recorded_ = true;
        return true;
    }

    bool DeferredRenderer::RecordPointShadowPass()
    {
        if (!active_frame_context_)
        {
            return false;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::PointShadow);
        if (!recorder || !shadow_target)
        {
            return false;
        }
        if (!active_point_shadow_.has_value())
        {
            return true;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            return false;
        }

        const PointShadowFrame &shadow = *active_point_shadow_;
        const std::vector<VisibleMeshSection> &proxies = BuildSectionCandidatesProfiled();
        std::vector<VisibleMeshSection> caster_candidates;
        caster_candidates.reserve(proxies.size());
        for (const VisibleMeshSection &candidate : proxies)
        {
            const MeshProxy &proxy = candidate.proxy;
            const std::optional<MaterialDrawClass> draw_class =
                material_system_->GetDrawClass(proxy.material);
            if (proxy.flags.visible && proxy.flags.casts_shadow &&
                IsBoundsInsideSphere(candidate.world_bounds, shadow.position, shadow.far_plane) &&
                draw_class.has_value() && *draw_class == MaterialDrawClass::Opaque &&
                material_system_->GetInstanceResolution(proxy.material).state ==
                    MaterialResourceState::Ready)
            {
                caster_candidates.push_back(candidate);
            }
        }
        const auto &faces = GetPointShadowFaceTable();
        std::array<uint32_t, 6> face_draw_counts{};
        for (size_t face_index = 0; face_index < faces.size(); ++face_index)
        {
            const PointShadowFaceDesc &face = faces[face_index];
            const Matrix4f view = Matrix4f::MakeCameraMatrix(shadow.position, face.direction, face.up);
            const Matrix4f projection = Matrix4f::MakePerProjMatrix(
                1.570796327f, 1.0f, shadow.near_plane, shadow.far_plane);
            recorder->SetViewport(graphics::Viewport{
                static_cast<float>(face.tile_x * kPointShadowFaceResolution),
                static_cast<float>(face.tile_y * kPointShadowFaceResolution),
                static_cast<float>(kPointShadowFaceResolution),
                static_cast<float>(kPointShadowFaceResolution), 0.0f, 1.0f});
            graphics::PerPassData per_pass_data{};
            per_pass_data.camera_data.view = view.Transpose();
            per_pass_data.camera_data.proj = projection.Transpose();
            const UniformAllocation per_pass = active_frame_context_->UpdateStableUniform(
                kPointShadowPerPassUniformKey + face_index, per_pass_data);
            if (!per_pass.IsValid())
            {
                return false;
            }
            for (const VisibleMeshSection &candidate : caster_candidates)
            {
                if (!camera::IsAABBInsidePerspectiveFace(
                        candidate.world_bounds, view, shadow.near_plane, shadow.far_plane))
                {
                    continue;
                }
                RecordShadowCaster(candidate.proxy, per_pass, *recorder,
                                   candidate.section_index);
                ++face_draw_counts[face_index];
            }
        }
        point_shadow_recorded_ = true;
        return true;
    }

    bool DeferredRenderer::RecordGBufferPass()
    {
        if (!active_frame_context_)
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        if (!recorder || !gbuffer_target)
        {
            return false;
        }
        const graphics::Extent2D extent = active_frame_context_->GetRenderExtent();
        if (extent.height != 0)
        {
            scene_camera_.SetAspect(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            const CameraData camera_data = scene_camera_.GetCameraData();
            graphics::PerPassData per_pass_data{};
            per_pass_data.camera_data.view = camera_data.view;
            per_pass_data.camera_data.proj = camera_data.proj;
            const std::vector<VisibleMeshSection> visible_sections =
                BuildVisibleSectionsProfiled(scene_camera_.GetViewProjectionMatrix());
            // Opaque-only for the deferred G-buffer; alpha-blended surfaces need
            // a forward pass (a later roadmap step), so they are skipped here.
            SceneDrawLists draw_lists = SceneDrawListBuilder::Build(
                visible_sections, *material_system_, *resource_resolver_, MaterialPass::GBuffer);
            SceneDrawListBuilder::SortOpaqueFrontToBack(
                draw_lists.opaque, scene_camera_.GetPosition(), scene_camera_.GetForward());
            const UniformAllocation per_pass = active_frame_context_->UpdateStableUniform(
                kGBufferPerPassUniformKey, per_pass_data);
            if (!per_pass.IsValid())
            {
                return false;
            }
            for (const SceneDrawItem &item : draw_lists.opaque)
            {
                if (RecordMeshProxy(item.proxy, per_pass, *recorder,
                                    MaterialPass::GBuffer, item.section_index))
                {
                    if (item.section_index != std::numeric_limits<uint32_t>::max())
                    {
                        const auto *const sections =
                            resource_resolver_->FindMeshSections(item.proxy.mesh);
                        if (sections != nullptr && item.section_index < sections->size())
                        {
                            triangle_count_ += (*sections)[item.section_index].index_count / 3U;
                        }
                    }
                    else
                    {
                        triangle_count_ += resource_resolver_->GetMeshTriangleCount(item.proxy.mesh);
                    }
                }
            }
        }
        return true;
    }

    bool DeferredRenderer::RecordDeferredLightingPass()
    {
        if (!active_frame_context_ || !frame_lighting_binding_.IsValid())
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const hdr_target = ResolveFrameTextureByName("SceneHdr");
        RenderTarget *const gbuffer_target = ResolveFrameTextureByName("GBuffer");
        RenderTarget *const shadow_target = ResolveFrameTextureByName("DirectionalShadow");
        RenderTarget *const spot_shadow_target = ResolveFrameTextureByName("SpotShadow");
        RenderTarget *const point_shadow_target = ResolveFrameTextureByName("PointShadow");
        if (!recorder || !hdr_target || !gbuffer_target || !shadow_target ||
            !spot_shadow_target || !point_shadow_target)
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Deferred lighting skipped: missing recorder or render target");
            return false;
        }
        if (!PrepareDeferredLightingPassResources())
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Deferred lighting skipped: resources are not ready");
            return false;
        }
        if (!hdr_target)
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Deferred lighting skipped: SceneHdr target could not begin recording");
            return false;
        }

        DeferredLightingGpuData lighting_data{};
        const bool ray_query_shadows =
            backend_->GetCapabilities().SupportsRayQueryShadows() &&
            backend_->GetActiveTopLevelAccelerationStructure().IsValid() &&
            deferred_lighting_ray_query_pipeline_.IsValid();
        profile_.ray_query_shadows_active = ray_query_shadows;
        if (ray_query_shadows != ray_query_shadow_path_active_)
        {
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "Deferred lighting shadow path: %s",
                   ray_query_shadows ? "ray_query" : "shadow_map_fallback");
            ray_query_shadow_path_active_ = ray_query_shadows;
        }
        const graphics::PipelineHandle lighting_pipeline = ray_query_shadows
                                                               ? deferred_lighting_ray_query_pipeline_
                                                               : deferred_lighting_pipeline_;
        lighting_data.inverse_view_projection =
            scene_camera_.GetViewProjectionMatrix().Inverse().Transpose();
        const Vector3f &camera_position = scene_camera_.GetPosition();
        lighting_data.camera_world_position = Vector4f{camera_position, 1.0f};
        lighting_data.environment_ibl_params = Vector4f{
            ray_query_shadows ? 0.0f : (active_environment_.ibl_enabled ? 1.0f : 0.0f),
            static_cast<float>(active_environment_.prefilter_level_count),
            active_environment_.ibl_intensity, 0.0f};
        if (active_directional_shadow_.has_value())
        {
            const DirectionalShadowFrame &shadow = *active_directional_shadow_;
            lighting_data.directional_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            lighting_data.directional_shadow_params =
                Vector4f{0.0005f,
                         0.002f,
                         1.0f / static_cast<float>(shadow.job.resolution),
                         0.0f};
        }
        if (active_spot_shadow_.has_value())
        {
            const SpotShadowFrame &shadow = *active_spot_shadow_;
            lighting_data.spot_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            lighting_data.spot_shadow_params =
                Vector4f{0.00075f, 0.003f,
                         1.0f / static_cast<float>(shadow.job.resolution), 1.0f};
        }
        PointShadowGpuData point_shadow_data{};
        if (active_point_shadow_.has_value() && point_shadow_recorded_)
        {
            point_shadow_data.face_view_projections =
                active_point_shadow_->face_view_projections;
            point_shadow_data.atlas_params = Vector4f{1.0f / 1536.0f, 1.0f / 1024.0f,
                                                      0.00075f, 0.003f};
        }
        const UniformAllocation lighting_constants =
            active_frame_context_->AllocateUniform(lighting_data);
        const UniformAllocation point_shadow_constants =
            active_frame_context_->AllocateUniform(point_shadow_data);
        bool recorded = false;
        if (lighting_constants.IsValid() && point_shadow_constants.IsValid())
        {
            std::vector<graphics::ResourceBinding> resource_bindings{
                graphics::SampledTextureBinding{
                          0, 0, gbuffer_target->GetColorAttachmentTexture(0),
                          gbuffer_debug_sampler_},
                      graphics::SampledTextureBinding{
                          0, 1, gbuffer_target->GetColorAttachmentTexture(1),
                          gbuffer_debug_sampler_},
                      graphics::SampledTextureBinding{
                          0, 2, gbuffer_target->GetColorAttachmentTexture(2),
                          gbuffer_debug_sampler_},
                      graphics::SampledTextureBinding{
                          0, 3, gbuffer_target->GetSampledDepthTexture(),
                          gbuffer_debug_sampler_},
                      frame_lighting_binding_.GetResourceBinding(),
                       graphics::UniformBufferBinding{
                          0, 5, lighting_constants.buffer, lighting_constants.offset,
                          lighting_constants.range},
                       graphics::UniformBufferBinding{
                           0, 13, point_shadow_constants.buffer, point_shadow_constants.offset,
                           point_shadow_constants.range},
                       graphics::SampledTextureBinding{
                           0, 6, shadow_target->GetSampledDepthTexture(),
                           directional_shadow_sampler_},
                       graphics::SampledTextureBinding{
                           0, 11, spot_shadow_target->GetSampledDepthTexture(),
                           spot_shadow_sampler_},
                       graphics::SampledTextureBinding{
                           0, 12, point_shadow_target->GetSampledDepthTexture(),
                           point_shadow_sampler_},
                       graphics::SampledTextureBinding{
                       0, 7, active_environment_.panorama.texture,
                           active_environment_.panorama.sampler},
                       graphics::SampledTextureBinding{
                           0, 8, active_environment_.irradiance.texture,
                           active_environment_.irradiance.sampler},
                       graphics::SampledTextureBinding{
                           0, 9, active_environment_.prefiltered_radiance.texture,
                           active_environment_.prefiltered_radiance.sampler},
                       graphics::SampledTextureBinding{
                           0, 10, active_environment_.brdf_lut.texture,
                           active_environment_.brdf_lut.sampler}};
            if (ray_query_shadows)
            {
                resource_bindings.emplace_back(graphics::AccelerationStructureBinding{
                    0, 14, backend_->GetActiveTopLevelAccelerationStructure()});
            }
            const graphics::DescriptorSetHandle bindings =
                active_frame_context_->AllocateResourceBindingSet(
                    lighting_pipeline, {0, std::move(resource_bindings)});
            if (bindings.IsValid())
            {
                recorder->BindPipeline(lighting_pipeline);
                recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
                recorder->BindResourceBindings(lighting_pipeline, bindings);
                recorder->DrawIndexed();
                AddProfileDraws(1, 1);
                recorded = true;
            }
        }
        return recorded;
    }

    bool DeferredRenderer::PrepareFullscreenPassResources()
    {
        if (gbuffer_debug_fullscreen_mesh_.IsValid() && gbuffer_debug_sampler_.IsValid())
        {
            return true;
        }

        data::MeshData fullscreen_mesh{};
        data::Vertex v0{}, v1{}, v2{};
        v0.position = {-1.0f, -1.0f, 0.0f};
        v0.tex_coord = {0.0f, 0.0f};
        v1.position = {3.0f, -1.0f, 0.0f};
        v1.tex_coord = {2.0f, 0.0f};
        v2.position = {-1.0f, 3.0f, 0.0f};
        v2.tex_coord = {0.0f, 2.0f};
        fullscreen_mesh.vertices = {v0, v1, v2};
        fullscreen_mesh.indices = {0, 1, 2};
        fullscreen_mesh.sections = {{0, 3, 0}};
        // Each handle is independently owned. Keep a successful half when the
        // other creation fails so a retry cannot overwrite it and leak the
        // already-created GPU object.
        if (!gbuffer_debug_fullscreen_mesh_.IsValid())
        {
            gbuffer_debug_fullscreen_mesh_ = backend_->CreateMesh(fullscreen_mesh);
        }
        if (!gbuffer_debug_sampler_.IsValid())
        {
            gbuffer_debug_sampler_ = backend_->CreateSampler(graphics::SamplerSettings{});
        }
        return gbuffer_debug_fullscreen_mesh_.IsValid() && gbuffer_debug_sampler_.IsValid();
    }

    bool DeferredRenderer::PrepareDeferredLightingPassResources()
    {
        const bool supports_ray_query = backend_->GetCapabilities().SupportsRayQueryShadows();
        if (deferred_lighting_pipeline_.IsValid() && directional_shadow_sampler_.IsValid() &&
            spot_shadow_sampler_.IsValid() && point_shadow_sampler_.IsValid() &&
            active_environment_.HasCompleteBindings() &&
            (!supports_ray_query || deferred_lighting_ray_query_pipeline_.IsValid()))
        {
            return true;
        }
        if (!PrepareFullscreenPassResources())
        {
            return false;
        }

        if (!directional_shadow_sampler_.IsValid())
        {
            graphics::SamplerSettings shadow_sampler_settings{};
            shadow_sampler_settings.address_mode_u =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_v =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_w =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.enable_anisotropy = false;
            directional_shadow_sampler_ = backend_->CreateSampler(shadow_sampler_settings);
        }
        if (!spot_shadow_sampler_.IsValid())
        {
            graphics::SamplerSettings shadow_sampler_settings{};
            shadow_sampler_settings.address_mode_u =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_v =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_w =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.enable_anisotropy = false;
            spot_shadow_sampler_ = backend_->CreateSampler(shadow_sampler_settings);
        }
        if (!point_shadow_sampler_.IsValid())
        {
            graphics::SamplerSettings shadow_sampler_settings{};
            shadow_sampler_settings.address_mode_u =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_v =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_w =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.enable_anisotropy = false;
            point_shadow_sampler_ = backend_->CreateSampler(shadow_sampler_settings);
        }
        if (!EnsureEnvironmentFallbackBindings())
        {
            return false;
        }
        if (!directional_shadow_sampler_.IsValid() || !spot_shadow_sampler_.IsValid() ||
            !point_shadow_sampler_.IsValid() ||
            !active_environment_.HasCompleteBindings())
        {
            return false;
        }
        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedProgram(BuiltInRenderAsset::DeferredLightingProgram, program))
        {
            return false;
        }
        const auto vert_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto frag_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status == asset::ShaderStatus::CompileFailed ||
            frag_shader->status == asset::ShaderStatus::CompileFailed)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA16F};
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {
            {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, position)},
            {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, tex_coord)},
        };
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
        desc.descriptor_binding_descs = {
            {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_FRAGMENT},
              {6, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {11, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {12, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {13, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {7, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {8, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {9, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {10, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT}},
        };
        if (!deferred_lighting_pipeline_.IsValid())
        {
            deferred_lighting_pipeline_ = backend_->CreatePipelineResource(desc);
        }
        if (!deferred_lighting_pipeline_.IsValid())
        {
            return false;
        }
        if (!supports_ray_query)
        {
            return true;
        }

        std::shared_ptr<const asset::ShaderProgramResource> ray_query_program;
        if (!GetPreparedProgram(BuiltInRenderAsset::DeferredLightingProgram,
                                ray_query_program, asset::ShaderProgramVariant::RayQuery))
        {
            return false;
        }
        const auto ray_query_vert_shader = prepared_assets_->Get<asset::ShaderResource>(
            ray_query_program->GetData(ShaderStage::SHADER_STAGE_VERTEX,
                                       ShaderFormat::SHADER_FORMAT_GLSL,
                                       asset::ShaderProgramVariant::RayQuery));
        const auto ray_query_frag_shader = prepared_assets_->Get<asset::ShaderResource>(
            ray_query_program->GetData(ShaderStage::SHADER_STAGE_FRAGMENT,
                                       ShaderFormat::SHADER_FORMAT_GLSL,
                                       asset::ShaderProgramVariant::RayQuery));
        if (!ray_query_vert_shader || !ray_query_frag_shader ||
            !ray_query_vert_shader->data || !ray_query_frag_shader->data ||
            ray_query_vert_shader->status == asset::ShaderStatus::CompileFailed ||
            ray_query_frag_shader->status == asset::ShaderStatus::CompileFailed)
        {
            return false;
        }
        graphics::PipelineDesc ray_query_desc = desc;
        ray_query_desc.vert_shader = ray_query_vert_shader->data.get();
        ray_query_desc.frag_shader = ray_query_frag_shader->data.get();
        ray_query_desc.descriptor_binding_descs[0].push_back({
            14, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE,
            ShaderStage::SHADER_STAGE_FRAGMENT});
        if (!deferred_lighting_ray_query_pipeline_.IsValid())
        {
            deferred_lighting_ray_query_pipeline_ =
                backend_->CreatePipelineResource(ray_query_desc);
        }
        return deferred_lighting_ray_query_pipeline_.IsValid();
    }

    bool DeferredRenderer::PrepareEnvironmentIbl(asset::AssetID source_asset,
                                             const data::TextureData &source,
                                             EnvironmentBindingBundle &bundle)
    {
        const PreparedEnvironmentIbl *const prepared =
            prepared_assets_ != nullptr ? prepared_assets_->FindEnvironmentIbl(source_asset)
                                         : nullptr;
        if (prepared == nullptr)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Environment IBL preprocessing requires a valid RGBA16F panorama");
            return false;
        }

        MaterialSamplerDesc panorama_sampler{};
        panorama_sampler.address_u = MaterialSamplerAddressMode::Repeat;
        panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        bundle.irradiance = resource_resolver_->GetOrCreateTextureBinding(
            source_asset, prepared->data.irradiance, MaterialTextureColorSpace::Linear,
            &panorama_sampler, TextureCacheVariant::EnvironmentIrradiance);
        bundle.prefiltered_radiance = resource_resolver_->GetOrCreateTextureBinding(
            source_asset, prepared->data.prefiltered_radiance,
            MaterialTextureColorSpace::Linear, &panorama_sampler,
            TextureCacheVariant::EnvironmentPrefilter);

        MaterialSamplerDesc lut_sampler{};
        lut_sampler.address_u = MaterialSamplerAddressMode::ClampToEdge;
        lut_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        lut_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        bundle.brdf_lut = resource_resolver_->GetOrCreateTextureBinding(
            source_asset, prepared->data.brdf_lut, MaterialTextureColorSpace::Linear,
            &lut_sampler, TextureCacheVariant::EnvironmentBrdfLut);
        bundle.prefilter_level_count = prepared->data.prefilter_level_count;
        bundle.ibl_enabled = bundle.HasCompleteBindings();
        return bundle.ibl_enabled;
    }

    bool DeferredRenderer::PrepareGBufferDebugPassResources()
    {
        if (gbuffer_debug_pipeline_.IsValid() && gbuffer_debug_fullscreen_mesh_.IsValid() &&
            gbuffer_debug_sampler_.IsValid())
        {
            return true;
        }
        if (!PrepareFullscreenPassResources())
        {
            return false;
        }

        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedProgram(BuiltInRenderAsset::GBufferDebugProgram, program))
        {
            return false;
        }
        const auto vert_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto frag_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA16F};
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {
            {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, position)},
            {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, tex_coord)},
        };
        // The common winding contract is translated by each backend, so this
        // shared CCW triangle remains front-facing on both APIs.
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
        desc.descriptor_binding_descs = {
            {{2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT}},
        };
        gbuffer_debug_pipeline_ = backend_->CreatePipelineResource(desc);

        if (!gbuffer_debug_pipeline_.IsValid() || !gbuffer_debug_fullscreen_mesh_.IsValid() ||
            !gbuffer_debug_sampler_.IsValid())
        {
            return false;
        }
        return true;
    }

    bool DeferredRenderer::RecordRayTracingPathTracePass()
    {
        if (!active_frame_context_ || !backend_ ||
            !ray_tracing_path_tracing_pipeline_.IsValid())
        {
            return false;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        graphics::RayTracingResourceOwner *const owner =
            backend_->GetRayTracingResourceOwner();
        RenderTarget *const hdr_target = ResolveFrameTextureByName("SceneHdr");
        RenderTarget *const history_target = ResolveFrameTextureByName("PathTraceHistory");
        const graphics::AccelerationStructureHandle top_level =
            backend_->GetActiveTopLevelAccelerationStructure();
        if (!recorder || !owner || !hdr_target || !history_target || !top_level.IsValid())
        {
            return false;
        }
        PathTracingCameraGpuData camera_data{};
        PathTracingSceneGpuData scene_gpu_data{};
        camera_data.inverse_view_projection =
            scene_camera_.GetViewProjectionMatrix().Inverse().Transpose();
        camera_data.camera_position = Vector4f{scene_camera_.GetPosition(), 1.0f};
        camera_data.rng_seed = kPathTraceRngSeed;
        camera_data.sample_count = path_trace_sample_count_;
        camera_data.samples_per_dispatch = kPathTraceSamplesPerDispatch;
        camera_data.probe_mode = static_cast<uint32_t>(path_trace_probe_mode_);
        camera_data.light_center = Vector4f{0.0f, 0.0f, 0.0f,
                                          static_cast<float>(kPathTraceDiffuseBounces)};
        camera_data.scene_data[0] = static_cast<uint32_t>(frame_ray_tracing_geometries_.size());
        camera_data.scene_data[1] = static_cast<uint32_t>(frame_ray_tracing_instance_data_.size());
        camera_data.scene_data[2] = static_cast<uint32_t>(frame_ray_tracing_material_data_.size());
        camera_data.scene_data[3] = static_cast<uint32_t>(frame_ray_tracing_light_data_.size());
        camera_data.light_radiance = Vector4f{0.0f, 0.0f, 0.0f,
                                              active_environment_.ibl_enabled
                                                  ? active_environment_.ibl_intensity
                                                  : 0.0f};
        if (frame_ray_tracing_geometries_.size() > kPathTraceMaximumSceneRecords ||
            frame_ray_tracing_instance_data_.size() > kPathTraceMaximumSceneRecords ||
            frame_ray_tracing_material_data_.size() > kPathTraceMaximumSceneRecords ||
            frame_ray_tracing_light_data_.size() > scene_gpu_data.light_data.size())
        {
            return false;
        }
        std::vector<graphics::RayTracingBufferAddressPatch> address_patches;
        address_patches.reserve(frame_ray_tracing_geometries_.size() * 2);
        for (std::size_t geometry = 0; geometry < frame_ray_tracing_geometries_.size(); ++geometry)
        {
            const graphics::RayTracingGeometryDesc &source =
                frame_ray_tracing_geometries_[geometry];
            if (source.index_type != graphics::RayTracingIndexType::UInt32)
            {
                return false;
            }
            PathTracingGeometryGpuData &destination = scene_gpu_data.geometry_data[geometry];
            destination.words = {
                0u, 0u, 0u, 0u,
                source.vertex_stride,
                static_cast<uint32_t>(source.index_type),
                static_cast<uint32_t>(offsetof(data::Vertex, tex_coord)),
                static_cast<uint32_t>(offsetof(data::Vertex, normal))};
            const std::size_t address_offset =
                offsetof(PathTracingSceneGpuData, geometry_data) +
                geometry * sizeof(PathTracingGeometryGpuData) +
                offsetof(PathTracingGeometryGpuData, words);
            address_patches.push_back(
                {address_offset, source.vertex_buffer, source.vertex_offset});
            address_patches.push_back(
                {address_offset + sizeof(uint64_t), source.index_buffer, source.index_offset});
        }
        for (std::size_t instance = 0;
             instance < frame_ray_tracing_instance_data_.size(); ++instance)
        {
            const RayTracingPathInstanceData &source =
                frame_ray_tracing_instance_data_[instance];
            PathTracingInstanceGpuData &destination = scene_gpu_data.instance_data[instance];
            destination.geometry_offset = source.geometry_offset;
            destination.material_offset = source.material_offset;
            destination.geometry_count = source.geometry_count;
        }
        for (std::size_t material = 0;
             material < frame_ray_tracing_material_data_.size(); ++material)
        {
            const RayTracingPathMaterialData &source =
                frame_ray_tracing_material_data_[material];
            PathTracingMaterialGpuData &destination = scene_gpu_data.material_data[material];
            destination.base_color = {source.base_color.x_, source.base_color.y_,
                                      source.base_color.z_, source.base_color.w_};
            destination.emissive = {source.emissive.x_, source.emissive.y_,
                                    source.emissive.z_, source.emissive.w_};
            destination.surface = {source.metallic, source.roughness,
                                   source.normal_scale, 0.0f};
            destination.surface[3] = static_cast<float>(source.metallic_channel + 4u * source.roughness_channel);
            destination.texture_indices[0] = source.base_color_texture_index;
            destination.texture_indices[1] = source.metallic_texture_index;
            destination.texture_indices[2] = source.roughness_texture_index;
        }
        for (std::size_t light = 0; light < frame_ray_tracing_light_data_.size(); ++light)
        {
            const RayTracingPathLightData &source = frame_ray_tracing_light_data_[light];
            PathTracingLightGpuData &destination = scene_gpu_data.light_data[light];
            destination.position_or_type = {source.position_or_type.x_,
                                            source.position_or_type.y_,
                                            source.position_or_type.z_,
                                            source.position_or_type.w_};
            destination.direction_and_range = {source.direction_and_range.x_,
                                               source.direction_and_range.y_,
                                               source.direction_and_range.z_,
                                               source.direction_and_range.w_};
            destination.color_intensity = {source.color_intensity.x_,
                                           source.color_intensity.y_,
                                           source.color_intensity.z_,
                                           source.color_intensity.w_};
            destination.parameters = {source.parameters.x_, source.parameters.y_,
                                      source.parameters.z_, source.parameters.w_};
        }
        const UniformAllocation camera_uniform = active_frame_context_->AllocateUniform(camera_data);
        if (!camera_uniform.IsValid())
        {
            return false;
        }

        if (ray_tracing_path_tracing_bindings_.IsValid())
        {
            owner->DestroyRayTracingResourceBindingSet(ray_tracing_path_tracing_bindings_);
            ray_tracing_path_tracing_bindings_ = {};
        }
        if (frame_ray_tracing_geometries_.empty())
        {
            return false;
        }
        std::vector<std::byte> scene_table_bytes(sizeof(scene_gpu_data));
        std::memcpy(scene_table_bytes.data(), &scene_gpu_data, sizeof(scene_gpu_data));
        std::vector<graphics::RayTracingResourceBinding> bindings{
            graphics::RayTracingAccelerationStructureBinding{0, 0, top_level},
            graphics::RayTracingStorageTextureBinding{
                0, 1, hdr_target->GetColorAttachmentTexture(0)},
            graphics::RayTracingStorageTextureBinding{
                0, 35, history_target->GetColorAttachmentTexture(0)},
            graphics::SampledTextureBinding{
                0, 4, active_environment_.panorama.texture,
                active_environment_.panorama.sampler},
            graphics::UniformBufferBinding{0, 2, camera_uniform.buffer,
                                           camera_uniform.offset, camera_uniform.range},
            graphics::RayTracingBufferReferenceTableBinding{
                0, 3, std::move(scene_table_bytes), std::move(address_patches)}};
        ray_tracing_path_tracing_bindings_ = owner->CreateRayTracingResourceBindingSet(
            ray_tracing_path_tracing_pipeline_, {0, std::move(bindings), false});
        if (!ray_tracing_path_tracing_bindings_.IsValid() ||
            !recorder->BindRayTracingPipeline(ray_tracing_path_tracing_pipeline_) ||
            !recorder->BindRayTracingResourceBindings(ray_tracing_path_tracing_bindings_))
        {
            return false;
        }
        graphics::RayTracingDispatchDesc dispatch{
            ray_tracing_path_tracing_pipeline_, ray_tracing_path_tracing_bindings_,
            hdr_target->GetWidth(), hdr_target->GetHeight(), 1};
        if (fail_next_path_trace_dispatch_)
        {
            fail_next_path_trace_dispatch_ = false;
            dispatch.width = 0;
            const bool unexpectedly_accepted = recorder->DispatchRays(dispatch);
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "R4.6 test injection passed a zero-width RT dispatch through Graphics; "
                   "rejected=%s (no vkCmdTraceRaysKHR call)",
                   unexpectedly_accepted ? "false" : "true");
            return false;
        }
        return recorder->DispatchRays(dispatch);
    }

    bool DeferredRenderer::RecordToneMapPass()
    {
        if (!active_frame_context_)
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const hdr_target = ResolveFrameTextureByName("SceneHdr");
        RenderTarget *const gbuffer_target = ResolveFrameTextureByName("GBuffer");
        RenderTarget *const scene_target = ResolveFrameTextureByName("SceneColor");
        if (!recorder || !hdr_target || !scene_target ||
            (!active_ray_tracing_path_trace_ && !gbuffer_target) ||
            !PrepareToneMapPassResources())
        {
            return false;
        }

        const UniformAllocation tone_map_options = active_frame_context_->AllocateUniform(
            Vector4f{active_ray_tracing_path_trace_ ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f});
        if (!tone_map_options.IsValid())
            return false;
        const graphics::DescriptorSetHandle tone_map_bindings =
            active_frame_context_->AllocateResourceBindingSet(
                tone_map_pipeline_,
                {0,
                  {graphics::SampledTextureBinding{
                      0, 2, hdr_target->GetColorAttachmentTexture(0),
                      gbuffer_debug_sampler_},
                   graphics::SampledTextureBinding{
                       0, 3,
                       active_ray_tracing_path_trace_
                           ? hdr_target->GetColorAttachmentTexture(0)
                           : gbuffer_target->GetColorAttachmentTexture(3),
                       gbuffer_debug_sampler_},
                   graphics::UniformBufferBinding{0, 4, tone_map_options.buffer,
                                                  tone_map_options.offset,
                                                  tone_map_options.range}}});
        if (tone_map_bindings.IsValid())
        {
            recorder->BindPipeline(tone_map_pipeline_);
            recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
            recorder->BindResourceBindings(tone_map_pipeline_, tone_map_bindings);
            recorder->DrawIndexed();
            AddProfileDraws(1, 1);
        }
        return tone_map_bindings.IsValid();
    }

    bool DeferredRenderer::RecordCaptureViewPass(CaptureView view)
    {
        if (!active_frame_context_ || view == CaptureView::SceneColor)
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const output_target = ResolveFrameTextureByName("CaptureOutput");
        RenderTarget *const gbuffer_target = ResolveFrameTextureByName("GBuffer");
        RenderTarget *const shadow_target = ResolveFrameTextureByName("DirectionalShadow");
        RenderTarget *const spot_shadow_target = ResolveFrameTextureByName("SpotShadow");
        RenderTarget *const point_shadow_target = ResolveFrameTextureByName("PointShadow");
        if (!recorder || !output_target || !gbuffer_target || !shadow_target ||
            !spot_shadow_target || !point_shadow_target ||
            !PrepareCaptureViewPassResources())
        {
            return false;
        }

        CaptureViewGpuData capture_data{};
        capture_data.inverse_view_projection =
            scene_camera_.GetViewProjectionMatrix().Inverse().Transpose();
        capture_data.view = scene_camera_.GetCameraData().view;
        const bool has_shadow = active_directional_shadow_.has_value();
        Vector3f surface_to_light{0.0f, 1.0f, 0.0f};
        if (has_shadow)
        {
            const DirectionalShadowFrame &shadow = *active_directional_shadow_;
            capture_data.directional_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            capture_data.directional_shadow_params =
                Vector4f{0.0005f,
                         0.002f,
                         1.0f / static_cast<float>(shadow.job.resolution),
                         0.0f};
            surface_to_light = -shadow.light_direction;
        }
        const bool has_spot_shadow = active_spot_shadow_.has_value() && spot_shadow_recorded_;
        if (has_spot_shadow)
        {
            const SpotShadowFrame &shadow = *active_spot_shadow_;
            capture_data.spot_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            capture_data.spot_shadow_params =
                Vector4f{0.00075f, 0.003f,
                         1.0f / static_cast<float>(shadow.job.resolution), 1.0f};
            if (!has_shadow)
            {
                surface_to_light = -shadow.light_direction;
            }
        }
        const bool has_point_shadow = active_point_shadow_.has_value() && point_shadow_recorded_;
        PointShadowGpuData point_shadow_data{};
        if (has_point_shadow)
        {
            point_shadow_data.face_view_projections = active_point_shadow_->face_view_projections;
            point_shadow_data.atlas_params = Vector4f{1.0f / 1536.0f, 1.0f / 1024.0f,
                                                      0.00075f, 0.003f};
            if (view == CaptureView::PointShadowVisibility)
            {
                surface_to_light = active_point_shadow_->position;
            }
        }
        capture_data.light_direction_and_view =
            Vector4f{surface_to_light, static_cast<float>(view)};
        capture_data.depth_params =
            Vector4f{scene_camera_.GetFarPlane(), has_shadow ? 1.0f : 0.0f,
                     has_spot_shadow ? 1.0f : 0.0f, has_point_shadow ? 1.0f : 0.0f};
        capture_data.punctual_depth_params = Vector4f{
            has_spot_shadow ? active_spot_shadow_->near_plane : 0.01f,
            has_spot_shadow ? active_spot_shadow_->far_plane : 1.0f,
            has_point_shadow ? active_point_shadow_->near_plane : 0.01f,
            has_point_shadow ? active_point_shadow_->far_plane : 1.0f};

        const UniformAllocation constants =
            active_frame_context_->AllocateUniform(capture_data);
        const UniformAllocation point_shadow_constants =
            active_frame_context_->AllocateUniform(point_shadow_data);
        if (!constants.IsValid() || !point_shadow_constants.IsValid())
        {
            return false;
        }

        const graphics::DescriptorSetHandle bindings =
            active_frame_context_->AllocateResourceBindingSet(
                capture_view_pipeline_,
                {0,
                 {graphics::SampledTextureBinding{
                      0, 2, gbuffer_target->GetColorAttachmentTexture(0),
                      gbuffer_debug_sampler_},
                  graphics::SampledTextureBinding{
                      0, 3, gbuffer_target->GetColorAttachmentTexture(1),
                      gbuffer_debug_sampler_},
                  graphics::SampledTextureBinding{
                      0, 4, gbuffer_target->GetColorAttachmentTexture(2),
                      gbuffer_debug_sampler_},
                  graphics::SampledTextureBinding{
                      0, 5, gbuffer_target->GetSampledDepthTexture(),
                      gbuffer_debug_sampler_},
                   graphics::SampledTextureBinding{
                       0, 6, shadow_target->GetSampledDepthTexture(),
                       directional_shadow_sampler_},
                   graphics::SampledTextureBinding{
                       0, 8, spot_shadow_target->GetSampledDepthTexture(),
                       spot_shadow_sampler_},
                  graphics::SampledTextureBinding{
                       0, 9, point_shadow_target->GetSampledDepthTexture(),
                       point_shadow_sampler_},
                  graphics::SampledTextureBinding{
                      0, 11, gbuffer_target->GetColorAttachmentTexture(3),
                      gbuffer_debug_sampler_},
                  graphics::UniformBufferBinding{
                       0, 7, constants.buffer, constants.offset, constants.range},
                   graphics::UniformBufferBinding{
                       0, 10, point_shadow_constants.buffer, point_shadow_constants.offset,
                       point_shadow_constants.range}}});
        if (!bindings.IsValid())
        {
            return false;
        }

        recorder->BindPipeline(capture_view_pipeline_);
        recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
        recorder->BindResourceBindings(capture_view_pipeline_, bindings);
        recorder->DrawIndexed();
        AddProfileDraws(1, 1);
        return true;
    }

    bool DeferredRenderer::PrepareToneMapPassResources()
    {
        if (tone_map_pipeline_.IsValid())
        {
            return true;
        }
        if (!PrepareFullscreenPassResources())
        {
            return false;
        }

        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedProgram(BuiltInRenderAsset::ToneMapProgram, program))
        {
            return false;
        }
        const auto vert_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto frag_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status == asset::ShaderStatus::CompileFailed ||
            frag_shader->status == asset::ShaderStatus::CompileFailed)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        tone_map_shader_signature_ = 1469598103934665603ull;
        AddShaderSignature(tone_map_shader_signature_, *vert_shader->data);
        AddShaderSignature(tone_map_shader_signature_, *frag_shader->data);
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {
            {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, position)},
            {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, tex_coord)},
        };
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
        desc.descriptor_binding_descs.resize(1);
        desc.descriptor_binding_descs[0] = {
            {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_FRAGMENT},
        };
        tone_map_pipeline_ = backend_->CreatePipelineResource(desc);
        return tone_map_pipeline_.IsValid();
    }

    bool DeferredRenderer::PrepareCaptureViewPassResources()
    {
        if (capture_view_pipeline_.IsValid() && directional_shadow_sampler_.IsValid() &&
            spot_shadow_sampler_.IsValid() && point_shadow_sampler_.IsValid())
        {
            return true;
        }
        if (!PrepareGBufferDebugPassResources())
        {
            return false;
        }

        if (!directional_shadow_sampler_.IsValid())
        {
            graphics::SamplerSettings shadow_sampler_settings{};
            shadow_sampler_settings.address_mode_u =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_v =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_w =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.enable_anisotropy = false;
            directional_shadow_sampler_ = backend_->CreateSampler(shadow_sampler_settings);
        }
        if (!spot_shadow_sampler_.IsValid())
        {
            graphics::SamplerSettings shadow_sampler_settings{};
            shadow_sampler_settings.address_mode_u =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_v =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_w =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.enable_anisotropy = false;
            spot_shadow_sampler_ = backend_->CreateSampler(shadow_sampler_settings);
        }
        if (!point_shadow_sampler_.IsValid())
        {
            graphics::SamplerSettings shadow_sampler_settings{};
            shadow_sampler_settings.address_mode_u =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_v =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.address_mode_w =
                graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            shadow_sampler_settings.enable_anisotropy = false;
            point_shadow_sampler_ = backend_->CreateSampler(shadow_sampler_settings);
        }
        if (!directional_shadow_sampler_.IsValid() || !spot_shadow_sampler_.IsValid() ||
            !point_shadow_sampler_.IsValid())
        {
            return false;
        }

        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedProgram(BuiltInRenderAsset::CaptureViewProgram, program))
        {
            return false;
        }
        const auto vert_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto frag_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status == asset::ShaderStatus::CompileFailed ||
            frag_shader->status == asset::ShaderStatus::CompileFailed)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {
            {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, position)},
            {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, tex_coord)},
        };
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
        desc.descriptor_binding_descs = {
            {{2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {11, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {6, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
              {8, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
               ShaderStage::SHADER_STAGE_FRAGMENT},
             {9, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {7, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {10, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_FRAGMENT}},
        };
        capture_view_pipeline_ = backend_->CreatePipelineResource(desc);
        return capture_view_pipeline_.IsValid();
    }

    bool DeferredRenderer::PrepareDirectionalShadowPassResources()
    {
        if (directional_shadow_pipeline_.IsValid())
        {
            return true;
        }

        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedProgram(BuiltInRenderAsset::DirectionalShadowProgram, program))
        {
            return false;
        }
        const auto vert_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto frag_shader = prepared_assets_->Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status == asset::ShaderStatus::CompileFailed ||
            frag_shader->status == asset::ShaderStatus::CompileFailed)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {{0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                             offsetof(data::Vertex, position)}};
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        // Conservative caster coverage; a portable depth-bias state is a
        // separate common-RHI extension, so this slice does not fake one.
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
        desc.descriptor_binding_descs = {
            {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC,
              ShaderStage::SHADER_STAGE_VERTEX},
             {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC,
              ShaderStage::SHADER_STAGE_VERTEX}},
        };
        directional_shadow_pipeline_ = backend_->CreatePipelineResource(desc);
        return directional_shadow_pipeline_.IsValid();
    }

    void DeferredRenderer::RecordShadowCaster(const MeshProxy &proxy,
                                          const UniformAllocation &per_pass,
                                          graphics::CommandRecorder &recorder,
                                          uint32_t section_index)
    {
        if (!proxy.flags.visible || !proxy.flags.casts_shadow || !proxy.mesh.IsValid() ||
            !directional_shadow_pipeline_.IsValid())
        {
            return;
        }
        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
        const UniformAllocation per_object = active_frame_context_->UpdateStableUniform(
            GetObjectUniformKey(proxy.handle), per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return;
        }
        const std::vector<graphics::ResourceBinding> draw_bindings{
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range}};
        const FrameResourceBinding resource_binding = active_frame_context_->CreateOrGetStableBindingSet(
            0x534841444f575f42ull, directional_shadow_pipeline_, draw_bindings);
        if (!resource_binding.IsValid())
        {
            return;
        }
        recorder.BindPipeline(directional_shadow_pipeline_);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(directional_shadow_pipeline_, resource_binding.descriptor_set,
                                      resource_binding.dynamic_offsets);
        const uint64_t draw_count =
            DrawMeshSections(*resource_resolver_, recorder, proxy.mesh, section_index);
        AddProfileDraws(draw_count, draw_count);
    }

    bool DeferredRenderer::RecordMeshProxy(const MeshProxy &proxy,
                                           const UniformAllocation &per_pass,
                                           graphics::CommandRecorder &recorder,
                                           MaterialPass pass, uint32_t section_index)
    {
        if (!active_frame_context_ || !proxy.flags.visible || !proxy.mesh.IsValid())
        {
            return false;
        }

        // A mesh's sections repeat the same per-object state, so resolve it once
        // per renderable per pass and reuse it.
        const uint64_t object_key =
            GetObjectUniformKey(proxy.handle) ^ static_cast<uint64_t>(pass);
        auto object_it = frame_object_states_.find(object_key);
        if (object_it == frame_object_states_.end())
        {
            FrameObjectState state{};
            graphics::PerObjectData per_object_data{};
            per_object_data.model =
                Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
            state.per_object = active_frame_context_->UpdateStableUniform(
                GetObjectUniformKey(proxy.handle), per_object_data);
            if (pass == MaterialPass::GBuffer)
            {
                const SelectionGpuData selection_data{
                    Vector4f{proxy.flags.selected ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}};
                state.selection = active_frame_context_->UpdateStableUniform(
                    GetSelectionUniformKey(proxy.handle), selection_data);
            }
            object_it = frame_object_states_.emplace(object_key, state).first;
        }
        const FrameObjectState &object_state = object_it->second;
        const UniformAllocation &per_object = object_state.per_object;
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return false;
        }

        // The binding is a pure function of the same renderable, its material,
        // the pass, and the per-pass block, so it can be resolved once too.
        const uint64_t binding_key =
            object_key ^ (static_cast<uint64_t>(proxy.material.id) << 1) ^
            (static_cast<uint64_t>(proxy.material.generation) << 17) ^ (per_pass.offset << 3);
        auto binding_it = frame_material_bindings_.find(binding_key);
        if (binding_it == frame_material_bindings_.end())
        {
            std::vector<graphics::ResourceBinding> draw_bindings{
                graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset,
                                               per_pass.range},
                graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset,
                                               per_object.range},
            };
            if (pass == MaterialPass::GBuffer)
            {
                if (!object_state.selection.IsValid())
                {
                    return false;
                }
                draw_bindings.emplace_back(graphics::UniformBufferBinding{
                    0, 9, object_state.selection.buffer, object_state.selection.offset,
                    object_state.selection.range});
            }
            const FrameMaterialBinding resolved = active_frame_context_->CreateMaterialBinding(
                *material_system_, *resource_resolver_, proxy.material, draw_bindings, pass);
            binding_it = frame_material_bindings_.emplace(binding_key, resolved).first;
        }
        const FrameMaterialBinding &material_binding = binding_it->second;
        if (!active_frame_context_->IsMaterialBindingCurrent(material_binding))
        {
            return false;
        }

        recorder.BindPipeline(material_binding.pipeline);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(material_binding.pipeline, material_binding.descriptor_set,
                                      material_binding.dynamic_offsets);
        const uint64_t draw_count =
            DrawMeshSections(*resource_resolver_, recorder, proxy.mesh, section_index);
        AddProfileDraws(draw_count, draw_count);
        return true;
    }

    void DeferredRenderer::AddProfileDraws(const uint64_t draw_calls,
                                           const uint64_t sections)
    {
        profile_.draw_calls += draw_calls;
        profile_.sections += sections;
        if (active_profile_pass_.has_value())
        {
            RenderProfilePassMetrics &pass = profile_.passes[*active_profile_pass_];
            pass.draw_calls += draw_calls;
            pass.sections += sections;
        }
    }

    bool DeferredRenderer::ResolveLevelEnvironment(const EnvironmentSourceDesc &source,
                                               EnvironmentBindingBundle &bundle)
    {
        if (!IsEnvironmentSourceDescValid(source))
        {
            return false;
        }

        const std::shared_ptr<const asset::TextureResource> texture_resource = prepared_assets_ != nullptr
            ? prepared_assets_->Get<asset::TextureResource>(source.texture_asset)
            : nullptr;
        if (texture_resource == nullptr || texture_resource->data == nullptr)
        {
            return false;
        }

        MaterialSamplerDesc panorama_sampler{};
        panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        bundle = {};
        bundle.source_asset = source.texture_asset;
        bundle.ibl_intensity = source.ibl_intensity;
        bundle.panorama = resource_resolver_->GetOrCreateTextureBinding(
            source.texture_asset, *texture_resource->data, MaterialTextureColorSpace::Srgb,
            &panorama_sampler);
        if (!bundle.panorama.texture.IsValid() || !bundle.panorama.sampler.IsValid())
        {
            return false;
        }
        return PrepareEnvironmentIbl(source.texture_asset, *texture_resource->data, bundle);
    }

    bool DeferredRenderer::EnsureEnvironmentFallbackBindings()
    {
        if (active_environment_.HasCompleteBindings())
        {
            return true;
        }

        data::TextureData fallback{};
        fallback.width = 1;
        fallback.height = 1;
        fallback.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
        fallback.pixels.resize(4 * sizeof(uint16_t), 0);

        EnvironmentBindingBundle black_fallback{};
        black_fallback.ibl_intensity = 0.25f;
        black_fallback.panorama = resource_resolver_->GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear);
        black_fallback.irradiance = resource_resolver_->GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
            TextureCacheVariant::EnvironmentIrradiance);
        black_fallback.prefiltered_radiance = resource_resolver_->GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
            TextureCacheVariant::EnvironmentPrefilter);
        black_fallback.brdf_lut = resource_resolver_->GetOrCreateTextureBinding(
            {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
            TextureCacheVariant::EnvironmentBrdfLut);
        black_fallback.ibl_enabled = false;
        if (!black_fallback.HasCompleteBindings())
        {
            return false;
        }
        active_environment_ = std::move(black_fallback);
        return true;
    }

    void DeferredRenderer::ApplyPendingSceneRenderTargetExtent()
    {
        if (!backend_ || pending_scene_render_target_extent_.width == 0 ||
            pending_scene_render_target_extent_.height == 0)
        {
            return;
        }

        const graphics::Extent2D requested = pending_scene_render_target_extent_;
        pending_scene_render_target_extent_ = {};
        const RenderTarget *const scene_target =
            frame_targets_.GetTarget(RenderTargetName::SceneColor);
        const bool extent_changed =
            scene_target == nullptr || scene_target->GetWidth() != requested.width ||
            scene_target->GetHeight() != requested.height;
        // RebuildForExtent retires via WaitIdle internally only when the extent
        // changed, so a stable size keeps the shared target's GPU generations
        // intact across frames. active_frame_context_ is nulled here to match the
        // old pre-rebuild boundary; the next BeginFrame re-acquires it.
        frame_targets_.RebuildForExtent(*backend_, requested.width, requested.height);
        if (extent_changed)
        {
            directional_shadow_valid_ = false;
            directional_shadow_stamp_ = 0;
            active_frame_context_ = nullptr;
        }
        if (!frame_targets_.GetTarget(RenderTargetName::SceneColor)->IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Failed to resize scene render target to %u x %u", requested.width,
                   requested.height);
        }
    }

}
