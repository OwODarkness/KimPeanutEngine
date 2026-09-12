#include "deferred_renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
        try
        {
            frame_targets_.Initialize(*backend_, width, height);
            if (!frame_targets_.IsValid())
            {
                throw std::runtime_error("Failed to create the complete render target set.");
            }
            ConfigurePassSequence();
            if (!pass_sequence_.has_value())
            {
                throw std::runtime_error("Fixed render pass sequence validation failed.");
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
        if (backend_ != nullptr)
        {
            if (gbuffer_debug_pipeline_.IsValid())
                backend_->DestroyPipelineResource(gbuffer_debug_pipeline_);
            if (capture_view_pipeline_.IsValid())
                backend_->DestroyPipelineResource(capture_view_pipeline_);
            if (deferred_lighting_pipeline_.IsValid())
                backend_->DestroyPipelineResource(deferred_lighting_pipeline_);
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
        gbuffer_debug_fullscreen_mesh_ = {};
        gbuffer_debug_sampler_ = {};
        directional_shadow_sampler_ = {};
        spot_shadow_sampler_ = {};
        point_shadow_sampler_ = {};
        tone_map_pipeline_ = {};
        directional_shadow_pipeline_ = {};
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
        pending_scene_render_target_extent_ = {};
        active_pass_frame_.reset();
        active_pending_capture_.reset();
        pass_sequence_.reset();
        frame_targets_.Cleanup();
        backend_ = nullptr;
        resource_resolver_ = nullptr;
        material_system_ = nullptr;
        prepared_assets_ = nullptr;
    }

    bool DeferredRenderer::GetPreparedProgram(BuiltInRenderAsset role,
                                              std::shared_ptr<const asset::ShaderProgramResource> &out_program) const
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
                asset::ShaderProgramVariant::Bound);
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

    DeferredRendererFrameResult DeferredRenderer::RecordFrame(
        FrameContext &frame_context, const RenderSceneFrameInput &input)
    {
        DeferredRendererFrameResult result{};
        triangle_count_ = 0;
        profile_ = {};
        profile_.frame_number = frame_context.GetGlobals().frame_number;
        profile_.graphics_api = backend_->GetGraphicsAPI();
        profile_.viewport_width = frame_context.GetRenderExtent().width;
        profile_.viewport_height = frame_context.GetRenderExtent().height;
        profile_.textures = resource_resolver_->GetTextureMetrics();
        material_system_->ResetProfileCounters();
        if (!pass_sequence_.has_value() || active_pass_frame_.has_value())
        {
            result.normal_recording_completed = false;
            return result;
        }
        active_frame_context_ = &frame_context;
        render_world_ = &input.render_world;
        frame_render_world_snapshot_ = render_world_->Snapshot();
        frame_section_packets_.clear();
        frame_section_packets_ready_ = false;
        scene_camera_ = input.camera;
        const std::optional<CaptureView> active_capture_view =
            input.pending_capture.has_value() ? input.pending_capture : input.debug_view;
        // Only a view this renderer converts itself is recorded here. A
        // host-resolved view is satisfied by the host's own target instead.
        const bool is_deferred_capture =
            active_capture_view.has_value() &&
            RequiresCaptureViewConversionPass(active_capture_view.value());
        active_pending_capture_ = is_deferred_capture ? active_capture_view : std::nullopt;
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
        active_pass_frame_.emplace(
            *pass_sequence_, is_deferred_capture);
        const bool cursor_started = active_pass_frame_->ExecuteRenderer(
            [this, &input](FixedRenderPassId id) { return ExecutePass(id, input.lights); });
        result.normal_recording_completed =
            cursor_started && !active_pass_frame_->HasRequiredFailure();
        if (input.pending_capture.has_value())
        {
            result.capture_target_ready =
                input.pending_capture.value() == CaptureView::SceneColor ||
                active_pass_frame_->GetOutcome(FixedRenderPassId::CaptureView) ==
                    RenderPassOutcome::Executed;
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
        case FixedRenderPassId::CaptureView:
            succeeded = active_pending_capture_.has_value() &&
                        RecordCaptureViewPass(*active_pending_capture_);
            break;
        case FixedRenderPassId::EditorComposite:
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
        if (finalized)
        {
            active_pass_frame_.reset();
            active_pending_capture_.reset();
            active_frame_context_ = nullptr;
            render_world_ = nullptr;
            frame_lighting_binding_ = {};
        }
        return finalized;
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

    void DeferredRenderer::ConfigurePassSequence()
    {
        std::vector<FixedRenderPassEntry> entries{
            {FixedRenderPassId::DirectionalShadow, "DirectionalShadowPass",
             {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::SpotShadow, "SpotShadowPass",
             {{RenderPassResource::SpotShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::PointShadow, "PointShadowPass",
             {{RenderPassResource::PointShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::GBuffer, "GBufferPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::DeferredLighting, "DeferredLightingPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
              {RenderPassResource::SpotShadow, RenderPassAccess::Read},
              {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneHdr, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::ToneMap, "ToneMapPass",
             {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::CaptureView, "CaptureViewPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
              {RenderPassResource::SpotShadow, RenderPassAccess::Read},
              {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Read},
              {RenderPassResource::CaptureOutput, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer,
             RenderPassCondition::DiagnosticCaptureRequested, false},
            {FixedRenderPassId::EditorComposite, "EditorCompositePass",
             {{RenderPassResource::SceneColor, RenderPassAccess::Read}},
             RenderPassExecutionOwner::External, RenderPassCondition::ExternalRequest, true},
        };
        std::string error;
        pass_sequence_ = FixedRenderPassSequence::Create(std::move(entries), error);
        if (!pass_sequence_.has_value())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Invalid fixed render pass sequence: %s",
                   error.c_str());
        }
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
        if (!recorder || !shadow_target || !shadow_target->BeginRecording(*recorder))
        {
            return false;
        }

        if (!active_directional_shadow_.has_value())
        {
            // Keep the always-bound fallback depth image in a valid sampled
            // layout when the directional fixture is intentionally disabled.
            shadow_target->EndRecording(*recorder);
            directional_shadow_valid_ = false;
            return true;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            shadow_target->EndRecording(*recorder);
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
            shadow_target->EndRecording(*recorder);
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
        shadow_target->EndRecording(*recorder);
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
        if (!recorder || !shadow_target || !shadow_target->BeginRecording(*recorder))
        {
            return false;
        }
        if (!active_spot_shadow_.has_value())
        {
            // Clear the fixed target even when the previous frame's selected
            // source was disabled, destroyed, stale, or over budget.
            shadow_target->EndRecording(*recorder);
            return true;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            shadow_target->EndRecording(*recorder);
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
            shadow_target->EndRecording(*recorder);
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
        shadow_target->EndRecording(*recorder);
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
        if (!recorder || !shadow_target || !shadow_target->BeginRecording(*recorder))
        {
            return false;
        }
        if (!active_point_shadow_.has_value())
        {
            shadow_target->EndRecording(*recorder);
            return true;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            shadow_target->EndRecording(*recorder);
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
                shadow_target->EndRecording(*recorder);
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
        shadow_target->EndRecording(*recorder);
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
        if (!recorder || !gbuffer_target || !gbuffer_target->BeginRecording(*recorder))
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
                gbuffer_target->EndRecording(*recorder);
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
        gbuffer_target->EndRecording(*recorder);
        return true;
    }

    bool DeferredRenderer::RecordDeferredLightingPass()
    {
        if (!active_frame_context_ || !frame_lighting_binding_.IsValid())
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const hdr_target = frame_targets_.GetTarget(RenderTargetName::SceneHdr);
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        RenderTarget *const shadow_target =
            frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
        RenderTarget *const spot_shadow_target =
            frame_targets_.GetTarget(RenderTargetName::SpotShadow);
        RenderTarget *const point_shadow_target =
            frame_targets_.GetTarget(RenderTargetName::PointShadow);
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
        if (!hdr_target->BeginRecording(*recorder))
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Deferred lighting skipped: SceneHdr target could not begin recording");
            return false;
        }

        DeferredLightingGpuData lighting_data{};
        lighting_data.inverse_view_projection =
            scene_camera_.GetViewProjectionMatrix().Inverse().Transpose();
        const Vector3f &camera_position = scene_camera_.GetPosition();
        lighting_data.camera_world_position = Vector4f{camera_position, 1.0f};
        lighting_data.environment_ibl_params = Vector4f{
            active_environment_.ibl_enabled ? 1.0f : 0.0f,
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
            const graphics::DescriptorSetHandle bindings =
                active_frame_context_->AllocateResourceBindingSet(
                    deferred_lighting_pipeline_,
                    {0,
                     {graphics::SampledTextureBinding{
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
                           active_environment_.brdf_lut.sampler}}});
            if (bindings.IsValid())
            {
                recorder->BindPipeline(deferred_lighting_pipeline_);
                recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
                recorder->BindResourceBindings(deferred_lighting_pipeline_, bindings);
                recorder->DrawIndexed();
                AddProfileDraws(1, 1);
                recorded = true;
            }
        }
        hdr_target->EndRecording(*recorder);
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
        if (deferred_lighting_pipeline_.IsValid() && directional_shadow_sampler_.IsValid() &&
            spot_shadow_sampler_.IsValid() && point_shadow_sampler_.IsValid() &&
            active_environment_.HasCompleteBindings())
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
        if (deferred_lighting_pipeline_.IsValid())
        {
            return true;
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
        deferred_lighting_pipeline_ = backend_->CreatePipelineResource(desc);
        return deferred_lighting_pipeline_.IsValid();
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

    bool DeferredRenderer::RecordToneMapPass()
    {
        if (!active_frame_context_)
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const hdr_target = frame_targets_.GetTarget(RenderTargetName::SceneHdr);
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        RenderTarget *const scene_target = frame_targets_.GetTarget(RenderTargetName::SceneColor);
        if (!recorder || !hdr_target || !gbuffer_target || !scene_target ||
            !PrepareToneMapPassResources() ||
            !scene_target->BeginRecording(*recorder))
        {
            return false;
        }

        const graphics::DescriptorSetHandle tone_map_bindings =
            active_frame_context_->AllocateResourceBindingSet(
                tone_map_pipeline_,
                {0,
                  {graphics::SampledTextureBinding{
                      0, 2, hdr_target->GetColorAttachmentTexture(0),
                      gbuffer_debug_sampler_},
                   graphics::SampledTextureBinding{
                       0, 3, gbuffer_target->GetColorAttachmentTexture(3),
                       gbuffer_debug_sampler_}}});
        if (tone_map_bindings.IsValid())
        {
            recorder->BindPipeline(tone_map_pipeline_);
            recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
            recorder->BindResourceBindings(tone_map_pipeline_, tone_map_bindings);
            recorder->DrawIndexed();
            AddProfileDraws(1, 1);
        }
        scene_target->EndRecording(*recorder);
        return tone_map_bindings.IsValid();
    }

    bool DeferredRenderer::RecordCaptureViewPass(CaptureView view)
    {
        if (!active_frame_context_ || view == CaptureView::SceneColor)
        {
            return false;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const output_target =
            frame_targets_.GetTarget(RenderTargetName::CaptureOutput);
        RenderTarget *const gbuffer_target =
            frame_targets_.GetTarget(RenderTargetName::GBuffer);
        RenderTarget *const shadow_target =
            frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
        RenderTarget *const spot_shadow_target =
            frame_targets_.GetTarget(RenderTargetName::SpotShadow);
        RenderTarget *const point_shadow_target =
            frame_targets_.GetTarget(RenderTargetName::PointShadow);
        if (!recorder || !output_target || !gbuffer_target || !shadow_target ||
            !spot_shadow_target || !point_shadow_target ||
            !PrepareCaptureViewPassResources() ||
            !output_target->BeginRecording(*recorder))
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
            output_target->EndRecording(*recorder);
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
            output_target->EndRecording(*recorder);
            return false;
        }

        recorder->BindPipeline(capture_view_pipeline_);
        recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
        recorder->BindResourceBindings(capture_view_pipeline_, bindings);
        recorder->DrawIndexed();
        AddProfileDraws(1, 1);
        output_target->EndRecording(*recorder);
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

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
        const UniformAllocation per_object = active_frame_context_->UpdateStableUniform(
            GetObjectUniformKey(proxy.handle), per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return false;
        }

        std::vector<graphics::ResourceBinding> draw_bindings{
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range},
        };
        if (pass == MaterialPass::GBuffer)
        {
            const SelectionGpuData selection_data{
                Vector4f{proxy.flags.selected ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}};
            const UniformAllocation selection =
                active_frame_context_->UpdateStableUniform(
                    GetSelectionUniformKey(proxy.handle), selection_data);
            if (!selection.IsValid())
            {
                return false;
            }
            draw_bindings.emplace_back(graphics::UniformBufferBinding{
                0, 9, selection.buffer, selection.offset, selection.range});
        }
        const FrameMaterialBinding material_binding = active_frame_context_->CreateMaterialBinding(
            *material_system_, *resource_resolver_, proxy.material, draw_bindings, pass);
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
