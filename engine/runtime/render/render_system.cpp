#include "render_system.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "config/path.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/command_recorder.h"
#include "log/logger.h"
#include "render/material/material_system.h"
#include "render/material/material_asset_resolver.h"
#include "render/render_capture_service_internal.h"
#include "render/render_world/scene_draw_list.h"
#include "render/render_world/scene_visibility.h"
#include "render_resource_resolver.h"
#include "resource/resource_pipeline.h"
#include "resource/shader_operation.h"

namespace kpengine::render
{
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

        // Runtime frame budget: cap the per-frame load+compile work so a burst of
        // requests never stalls a frame. 0 means "no budget" (the bootstrap pass).
        constexpr std::size_t kMaxRuntimeLoadsPerFrame = 2;
        constexpr uint32_t kPointShadowFaceResolution = 512;
        constexpr uint32_t kPointShadowAtlasWidth = kPointShadowFaceResolution * 3;
        constexpr uint32_t kPointShadowAtlasHeight = kPointShadowFaceResolution * 2;
        constexpr uint64_t kPointShadowTargetBytes =
            static_cast<uint64_t>(kPointShadowAtlasWidth) * kPointShadowAtlasHeight * 4;

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
        };

        spatial::AABB TransformBounds(const spatial::AABB &local_bounds,
                                      const Transform3f &transform)
        {
            if (!local_bounds.IsValid())
            {
                return local_bounds;
            }

            const std::array<Vector3f, 8> corners{{
                {local_bounds.min_.x_, local_bounds.min_.y_, local_bounds.min_.z_},
                {local_bounds.min_.x_, local_bounds.min_.y_, local_bounds.max_.z_},
                {local_bounds.min_.x_, local_bounds.max_.y_, local_bounds.min_.z_},
                {local_bounds.min_.x_, local_bounds.max_.y_, local_bounds.max_.z_},
                {local_bounds.max_.x_, local_bounds.min_.y_, local_bounds.min_.z_},
                {local_bounds.max_.x_, local_bounds.min_.y_, local_bounds.max_.z_},
                {local_bounds.max_.x_, local_bounds.max_.y_, local_bounds.min_.z_},
                {local_bounds.max_.x_, local_bounds.max_.y_, local_bounds.max_.z_},
            }};

            const float maximum = std::numeric_limits<float>::max();
            spatial::AABB world_bounds{{maximum, maximum, maximum},
                                       {-maximum, -maximum, -maximum}};
            for (const Vector3f &corner : corners)
            {
                const Vector3f transformed =
                    transform.rotator_.RotateVector(transform.scale_ * corner) +
                    transform.position_;
                world_bounds.min_.x_ = std::min(world_bounds.min_.x_, transformed.x_);
                world_bounds.min_.y_ = std::min(world_bounds.min_.y_, transformed.y_);
                world_bounds.min_.z_ = std::min(world_bounds.min_.z_, transformed.z_);
                world_bounds.max_.x_ = std::max(world_bounds.max_.x_, transformed.x_);
                world_bounds.max_.y_ = std::max(world_bounds.max_.y_, transformed.y_);
                world_bounds.max_.z_ = std::max(world_bounds.max_.z_, transformed.z_);
            }
            return world_bounds;
        }

        std::array<Vector3f, 8> GetBoundsCorners(const spatial::AABB &bounds)
        {
            return {{
                {bounds.min_.x_, bounds.min_.y_, bounds.min_.z_},
                {bounds.min_.x_, bounds.min_.y_, bounds.max_.z_},
                {bounds.min_.x_, bounds.max_.y_, bounds.min_.z_},
                {bounds.min_.x_, bounds.max_.y_, bounds.max_.z_},
                {bounds.max_.x_, bounds.min_.y_, bounds.min_.z_},
                {bounds.max_.x_, bounds.min_.y_, bounds.max_.z_},
                {bounds.max_.x_, bounds.max_.y_, bounds.min_.z_},
                {bounds.max_.x_, bounds.max_.y_, bounds.max_.z_},
            }};
        }

        void ExpandBounds(spatial::AABB &bounds, const Vector3f &point)
        {
            bounds.min_.x_ = std::min(bounds.min_.x_, point.x_);
            bounds.min_.y_ = std::min(bounds.min_.y_, point.y_);
            bounds.min_.z_ = std::min(bounds.min_.z_, point.z_);
            bounds.max_.x_ = std::max(bounds.max_.x_, point.x_);
            bounds.max_.y_ = std::max(bounds.max_.y_, point.y_);
            bounds.max_.z_ = std::max(bounds.max_.z_, point.z_);
        }

        std::optional<spatial::AABB> BuildDirectionalShadowBounds(
            const std::vector<MeshProxy> &proxies, const Vector3f &camera_position)
        {
            const float maximum = std::numeric_limits<float>::max();
            spatial::AABB bounds{{maximum, maximum, maximum},
                                 {-maximum, -maximum, -maximum}};
            bool has_caster = false;
            for (const MeshProxy &proxy : proxies)
            {
                if (!proxy.flags.visible || !proxy.flags.casts_shadow ||
                    !proxy.world_bounds.IsValid())
                {
                    continue;
                }
                ExpandBounds(bounds, proxy.world_bounds.min_);
                ExpandBounds(bounds, proxy.world_bounds.max_);
                has_caster = true;
            }
            if (!has_caster)
            {
                return std::nullopt;
            }

            // Keep the current view origin inside the fitted region. The complete
            // receiver volume is a later cascade/receiver-fit concern, but this
            // prevents an off-camera caster fit from immediately losing the view.
            ExpandBounds(bounds, camera_position);
            return bounds;
        }

        struct DirectionalShadowMatrices
        {
            Matrix4f view;
            Matrix4f projection;
        };

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
            for (const Vector3f &corner : GetBoundsCorners(caster_bounds))
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
            for (const Vector3f &corner : GetBoundsCorners(bounds))
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

        bool IsPointBoundsInsideFace(const spatial::AABB &bounds,
                                     const Matrix4f &view, float near_plane, float far_plane)
        {
            if (!bounds.IsValid())
            {
                return true;
            }
            for (const Vector3f &corner : GetBoundsCorners(bounds))
            {
                const Vector4f light_space = view * Vector4f{corner, 1.0f};
                const float depth = -light_space.z_;
                if (depth >= near_plane && depth <= far_plane &&
                    std::abs(light_space.x_) <= depth && std::abs(light_space.y_) <= depth)
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
    }

    RenderSystem::RenderSystem() = default;

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    void RenderSystem::Initialize(const RenderSystemInitInfo &info)
    {
        if (!info.native_window || !info.resize_dispatcher || !info.load_queue)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "RenderSystem initialization requires window, resize dispatcher, and load queue");
            return;
        }
        load_queue_ = info.load_queue;
        bootstrap_scene_info_ = info.bootstrap_scene;
        environment_ibl_intensity_ = bootstrap_scene_info_.environment_ibl_intensity;
        material_system_ = std::make_unique<MaterialSystem>();
        material_asset_resolver_ = std::make_unique<MaterialAssetResolver>(*material_system_);

        resource_pipeline_ = std::make_unique<resource::ResourcePipeline>();
        resource::ResourcePipelineContext context;
        context.graphics_type = info.api_type;
        resource_pipeline_->Initialize(context);

        backend_ = graphics::RenderBackend::CreateGraphicsBackEnd(info.api_type);
        if (!backend_)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "No graphics backend for API %d",
                   static_cast<int>(info.api_type));
            return;
        }
        KP_LOG("RenderLog", LOG_LEVEL_INFO, "RenderSystem selected %s graphics backend",
               GetGraphicsApiName(info.api_type));
        backend_->BindWindowResize(*info.resize_dispatcher);
        backend_->Initialize(info.native_window);


        resource_resolver_ =
            std::make_unique<RenderResourceResolver>(*backend_, *resource_pipeline_);
        material_system_->SetResourceResolver(resource_resolver_.get());

        constexpr size_t kFrameUniformCapacity = 64 * 1024;
        frame_contexts_.resize(backend_->GetFramesInFlight());
        for (FrameContext &context : frame_contexts_)
        {
            context.Initialize(*backend_, kFrameUniformCapacity);
        }

        const graphics::Extent2D extent = backend_->GetRenderExtent();
        frame_targets_.Initialize(*backend_, extent.width, extent.height);
        if (!frame_targets_.GetTarget(RenderTargetName::SceneColor)->IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Failed to create scene render target");
        }
        render_capture_service_ = std::make_unique<RenderCaptureService>(
            backend_->GetRenderTargetReadback(),
            [this](CaptureView view)
            {
                const RenderTargetName target_name = view == CaptureView::SceneColor
                                                         ? RenderTargetName::SceneColor
                                                         : RenderTargetName::CaptureOutput;
                const RenderTarget *const target = frame_targets_.GetTarget(target_name);
                return target ? target->GetHandle() : graphics::RenderTargetHandle{};
            },
            [this] { return frame_number_; });
        ConfigurePassSchedule();
        // Frame the ~165-unit rock bootstrap fixture: pull the camera back and
        // extend the far plane (the old default far=10 culled it entirely).
        ApplyDefaultCamera();
    }

    void RenderSystem::PostInitialize()
    {
        // Bootstrap: load + process everything already queued, before the main loop.
        ConsumeRequests(0);
        PrepareBootstrapRenderableSources();
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "Bootstrap drained: %d distinct shader(s) loaded", GetLoadedShaderCount());
    }

    void RenderSystem::Tick(float delta_time)
    {
        BeginFrame(delta_time);
        EndFrame();
    }

    void RenderSystem::BeginFrame(float delta_time)
    {
        ConsumeRequests(kMaxRuntimeLoadsPerFrame);
        material_system_->RefreshResources();
        DrainRenderableSources();
        render_world_.ApplyPendingCommands();
        DrainLightSources();
        DrainCameraSources();
        const std::vector<Light> light_snapshot = light_world_.Snapshot();
        active_directional_shadow_ = ScheduleDirectionalShadow(light_snapshot);
        active_spot_shadow_ = ScheduleSpotShadow(light_snapshot);
        active_point_shadow_ = SchedulePointShadow(light_snapshot);
        if (!backend_)
        {
            return;
        }

        ApplyPendingSceneRenderTargetExtent();
        backend_->BeginFrame();
        active_frame_context_ = GetCurrentFrameContext();
        if (active_frame_context_)
        {
            elapsed_seconds_ += delta_time;
            active_frame_context_->Begin(
                backend_->GetCurrentFrameIndex(),
                {frame_number_, elapsed_seconds_, delta_time},
                {frame_targets_.GetTarget(RenderTargetName::SceneColor)->GetWidth(),
                 frame_targets_.GetTarget(RenderTargetName::SceneColor)->GetHeight()});
            editor_composite_recorded_ = false;
            spot_shadow_recorded_ = false;
            point_shadow_recorded_ = false;
            RecordDirectionalShadowPass();
            RecordSpotShadowPass();
            RecordPointShadowPass();
            ResolvedLightShadowBindings resolved_shadows;
            if (active_directional_shadow_.has_value())
            {
                const DirectionalShadowFrame &shadow = *active_directional_shadow_;
                resolved_shadows.push_back(ResolvedLightShadowBinding{
                    shadow.job.source_light, shadow.shadow, shadow.job.kind,
                    shadow.job.binding_slot});
            }
            if (active_spot_shadow_.has_value() && spot_shadow_recorded_)
            {
                const SpotShadowFrame &shadow = *active_spot_shadow_;
                resolved_shadows.push_back(ResolvedLightShadowBinding{
                    shadow.job.source_light, shadow.shadow, shadow.job.kind,
                    shadow.job.binding_slot});
            }
            if (active_point_shadow_.has_value() && point_shadow_recorded_)
            {
                const PointShadowFrame &shadow = *active_point_shadow_;
                resolved_shadows.push_back(ResolvedLightShadowBinding{
                    shadow.job.source_light, shadow.shadow, shadow.job.kind,
                    shadow.job.binding_slot});
            }
            frame_lighting_binding_ = active_frame_context_->CreateLightingBinding(
                BuildLightGpuFrameData(light_snapshot, resolved_shadows));
            RecordGBufferPass();
            RecordDeferredLightingPass();
            RecordToneMapPass();
            RecordPendingCapturePass();
        }
    }

    void RenderSystem::EndFrame()
    {
        if (!backend_)
        {
            return;
        }
        if (active_frame_context_)
        {
            active_frame_context_->End();
            active_frame_context_ = nullptr;
            frame_lighting_binding_ = {};
            ++frame_number_;
        }
        backend_->EndFrame();
    }

    bool RenderSystem::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!active_frame_context_ || editor_composite_recorded_ || !record_pass ||
            !pass_schedule_.IsValid())
        {
            return false;
        }
        record_pass();
        editor_composite_recorded_ = true;
        return true;
    }

    void RenderSystem::RequestSceneRenderTargetExtent(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        pending_scene_render_target_extent_ = {width, height};
    }

    GraphicsContext RenderSystem::GetGraphicsContext()
    {
        return backend_ ? backend_->GetGraphicsContext()
                        : GraphicsContext{GraphicsAPIType::GRAPHICS_API_UNKNOW, nullptr};
    }

    bool RenderSystem::IsReady(asset::RequestID request_id) const
    {
        return render_cache_.find(request_id) != render_cache_.end();
    }

    const RenderCacheEntry *RenderSystem::GetCached(asset::RequestID request_id) const
    {
        auto it = render_cache_.find(request_id);
        return it == render_cache_.end() ? nullptr : &it->second;
    }

    graphics::PipelineHandle RenderSystem::GetPipeline(asset::RequestID request_id) const
    {
        const RenderCacheEntry *entry = GetCached(request_id);
        const auto *pipeline = entry ? std::get_if<graphics::PipelineHandle>(&entry->resource) : nullptr;
        return pipeline ? *pipeline : graphics::PipelineHandle{};
    }

    int RenderSystem::GetLoadedShaderCount() const
    {
        return resource_pipeline_
                   ? static_cast<int>(resource_pipeline_->GetProcessedShaderCount())
                   : 0;
    }

    IRenderCaptureService *RenderSystem::GetRenderCaptureService()
    {
        return render_capture_service_.get();
    }

    void RenderSystem::ConfigurePassSchedule()
    {
        // D5.2 fixed schedule: deferred lighting is the sole SceneHdr writer;
        // ToneMapPass remains the sole SceneColor writer before the editor's
        // terminal read. The debug conversion is retained outside the normal
        // schedule for later explicit capture-view routing.
        const bool added_shadow = pass_schedule_.AddPass(
            {"ShadowDepthPass", {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write},
                                  {RenderPassResource::SpotShadow, RenderPassAccess::Write},
                                  {RenderPassResource::PointShadow, RenderPassAccess::Write}}, false});
        const bool added_gbuffer = pass_schedule_.AddPass(
            {"GBufferPass", {{RenderPassResource::GBuffer, RenderPassAccess::Write}}, false});
        const bool added_deferred_lighting = pass_schedule_.AddPass(
            {"DeferredLightingPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
               {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
               {RenderPassResource::SpotShadow, RenderPassAccess::Read},
               {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneHdr, RenderPassAccess::Write}},
             false});
        const bool added_tone_map = pass_schedule_.AddPass(
            {"ToneMapPass",
             {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Write}},
             false});
        const bool added_capture = pass_schedule_.AddPass(
            {"CaptureViewPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
               {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
               {RenderPassResource::SpotShadow, RenderPassAccess::Read},
               {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Read},
              {RenderPassResource::CaptureOutput, RenderPassAccess::Write}},
             false});
        const bool added_editor = pass_schedule_.AddPass(
            {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true});
        std::string error;
        if (!added_shadow || !added_gbuffer || !added_deferred_lighting ||
            !added_tone_map || !added_capture || !added_editor ||
            !pass_schedule_.Validate(error))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Invalid render pass schedule: %s", error.c_str());
        }
    }

    std::optional<RenderSystem::DirectionalShadowFrame> RenderSystem::ScheduleDirectionalShadow(
        const std::vector<Light> &lights) const
    {
        // The first implementation schedules at most one requested directional
        // map. The source's private ShadowHandle is the opt-in; absent, disabled,
        // or malformed records intentionally remain unshadowed.
        constexpr uint32_t kDirectionalShadowResolution = 2048;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Directional ||
                !light.desc.shadow.has_value() ||
                !light_source_registry_.IsShadowHandleValid(*light.desc.shadow) ||
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
            const std::vector<MeshProxy> caster_candidates = render_world_.Snapshot();
            DirectionalShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::Directional2D,
                         kDirectionalShadowResolution, 0};
            frame.shadow = *light.desc.shadow;
            frame.light_direction = direction;
            if (const std::optional<spatial::AABB> caster_bounds =
                    BuildDirectionalShadowBounds(caster_candidates, scene_camera_.GetPosition()))
            {
                const DirectionalShadowMatrices matrices =
                    FitDirectionalShadowMatrices(*caster_bounds, direction);
                frame.view = matrices.view;
                frame.projection = matrices.projection;
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
            }
            return frame;
        }
        return std::nullopt;
    }

    std::optional<RenderSystem::SpotShadowFrame> RenderSystem::ScheduleSpotShadow(
        const std::vector<Light> &lights) const
    {
        constexpr uint32_t kSpotShadowResolution = 1024;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Spot ||
                !light.desc.shadow.has_value() ||
                !light_source_registry_.IsShadowHandleValid(*light.desc.shadow) ||
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
            for (const MeshProxy &proxy : render_world_.Snapshot())
            {
                const std::optional<MaterialDrawClass> draw_class =
                    material_system_->GetDrawClass(proxy.material);
                if (proxy.flags.visible && proxy.flags.casts_shadow &&
                    IsSpotBoundsInsideFrustum(proxy.world_bounds, frame.view,
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

    std::optional<RenderSystem::PointShadowFrame> RenderSystem::SchedulePointShadow(
        const std::vector<Light> &lights) const
    {
        const std::vector<MeshProxy> proxies = render_world_.Snapshot();
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Point ||
                !light.desc.shadow.has_value() ||
                !light_source_registry_.IsShadowHandleValid(*light.desc.shadow) ||
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
            for (const MeshProxy &proxy : proxies)
            {
                const std::optional<MaterialDrawClass> draw_class =
                    material_system_->GetDrawClass(proxy.material);
                const auto resolution = material_system_->GetInstanceResolution(proxy.material);
                if (proxy.flags.visible && proxy.flags.casts_shadow &&
                    IsBoundsInsideSphere(proxy.world_bounds, point->position, point->range) &&
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

    void RenderSystem::RecordDirectionalShadowPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
        if (!recorder || !shadow_target || !shadow_target->BeginRecording(*recorder))
        {
            return;
        }

        if (!active_directional_shadow_.has_value())
        {
            // Keep the always-bound fallback depth image in a valid sampled
            // layout when the directional fixture is intentionally disabled.
            shadow_target->EndRecording(*recorder);
            return;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            shadow_target->EndRecording(*recorder);
            return;
        }

        const DirectionalShadowFrame &shadow = *active_directional_shadow_;
        graphics::PerPassData per_pass_data{};
        per_pass_data.camera_data.view = shadow.view.Transpose();
        per_pass_data.camera_data.proj = shadow.projection.Transpose();
        // Camera visibility remains a G-buffer optimization. The fitted shadow
        // volume and this pass both consume the complete caster snapshot.
        const std::vector<MeshProxy> shadow_caster_candidates = render_world_.Snapshot();
        for (const MeshProxy &proxy : shadow_caster_candidates)
        {
            const std::optional<MaterialDrawClass> draw_class =
                material_system_->GetDrawClass(proxy.material);
            if (proxy.flags.casts_shadow && draw_class.has_value() &&
                *draw_class == MaterialDrawClass::Opaque &&
                material_system_->GetInstanceResolution(proxy.material).state == MaterialResourceState::Ready)
            {
                RecordShadowCaster(proxy, per_pass_data, *recorder);
            }
        }
        shadow_target->EndRecording(*recorder);
    }

    void RenderSystem::RecordSpotShadowPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::SpotShadow);
        if (!recorder || !shadow_target || !shadow_target->BeginRecording(*recorder))
        {
            return;
        }
        if (!active_spot_shadow_.has_value())
        {
            // Clear the fixed target even when the previous frame's selected
            // source was disabled, destroyed, stale, or over budget.
            shadow_target->EndRecording(*recorder);
            return;
        }
        if (!PrepareDirectionalShadowPassResources())
        {
            shadow_target->EndRecording(*recorder);
            return;
        }
        const SpotShadowFrame &shadow = *active_spot_shadow_;
        graphics::PerPassData per_pass_data{};
        per_pass_data.camera_data.view = shadow.view.Transpose();
        per_pass_data.camera_data.proj = shadow.projection.Transpose();
        for (const MeshProxy &proxy : render_world_.Snapshot())
        {
            const std::optional<MaterialDrawClass> draw_class =
                material_system_->GetDrawClass(proxy.material);
            if (!proxy.flags.visible || !proxy.flags.casts_shadow ||
                !IsSpotBoundsInsideFrustum(proxy.world_bounds, shadow.view,
                                           shadow.outer_cone_radians,
                                           shadow.near_plane, shadow.far_plane) ||
                !draw_class.has_value() || *draw_class != MaterialDrawClass::Opaque ||
                material_system_->GetInstanceResolution(proxy.material).state !=
                    MaterialResourceState::Ready)
            {
                continue;
            }
            RecordShadowCaster(proxy, per_pass_data, *recorder);
        }
        shadow_target->EndRecording(*recorder);
        spot_shadow_recorded_ = true;
    }

    void RenderSystem::RecordPointShadowPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::PointShadow);
        if (!recorder || !shadow_target || !shadow_target->BeginRecording(*recorder))
        {
            return;
        }
        if (!active_point_shadow_.has_value() || !PrepareDirectionalShadowPassResources())
        {
            shadow_target->EndRecording(*recorder);
            return;
        }

        const PointShadowFrame &shadow = *active_point_shadow_;
        const auto profile_start = std::chrono::steady_clock::now();
        const std::vector<MeshProxy> proxies = render_world_.Snapshot();
        std::vector<MeshProxy> caster_candidates;
        caster_candidates.reserve(proxies.size());
        for (const MeshProxy &proxy : proxies)
        {
            const std::optional<MaterialDrawClass> draw_class =
                material_system_->GetDrawClass(proxy.material);
            if (proxy.flags.visible && proxy.flags.casts_shadow &&
                IsBoundsInsideSphere(proxy.world_bounds, shadow.position, shadow.far_plane) &&
                draw_class.has_value() && *draw_class == MaterialDrawClass::Opaque &&
                material_system_->GetInstanceResolution(proxy.material).state ==
                    MaterialResourceState::Ready)
            {
                caster_candidates.push_back(proxy);
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
            for (const MeshProxy &proxy : caster_candidates)
            {
                if (!IsPointBoundsInsideFace(proxy.world_bounds, view,
                                             shadow.near_plane, shadow.far_plane))
                {
                    continue;
                }
                RecordShadowCaster(proxy, per_pass_data, *recorder);
                ++face_draw_counts[face_index];
            }
        }
        shadow_target->EndRecording(*recorder);
        point_shadow_recorded_ = true;
        if (!point_shadow_profile_logged_)
        {
            uint32_t total_draw_count = 0;
            uint32_t empty_face_count = 0;
            for (const uint32_t draw_count : face_draw_counts)
            {
                total_draw_count += draw_count;
                empty_face_count += draw_count == 0 ? 1U : 0U;
            }
            const auto profile_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - profile_start);
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "Point shadow profile: face_draws=[%u,%u,%u,%u,%u,%u] total_draws=%u "
                   "empty_faces=%u candidates=%zu cpu_record_us=%llu target=%ux%u D32 "
                   "target_bytes=%llu gpu_time=unavailable",
                   face_draw_counts[0], face_draw_counts[1], face_draw_counts[2],
                   face_draw_counts[3], face_draw_counts[4], face_draw_counts[5],
                   total_draw_count, empty_face_count, caster_candidates.size(),
                   static_cast<unsigned long long>(profile_duration.count()),
                   kPointShadowAtlasWidth, kPointShadowAtlasHeight,
                   static_cast<unsigned long long>(kPointShadowTargetBytes));
            point_shadow_profile_logged_ = true;
        }
    }

    void RenderSystem::RecordGBufferPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        if (!recorder || !gbuffer_target || !gbuffer_target->BeginRecording(*recorder))
        {
            return;
        }
        const graphics::Extent2D extent = active_frame_context_->GetRenderExtent();
        if (extent.height != 0)
        {
            scene_camera_.SetAspect(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            const CameraData camera_data = scene_camera_.GetCameraData();
            graphics::PerPassData per_pass_data{};
            per_pass_data.camera_data.view = camera_data.view;
            per_pass_data.camera_data.proj = camera_data.proj;
            const std::vector<MeshProxy> visible_proxies = SceneVisibility::BuildVisibleProxies(
                scene_camera_.GetViewProjectionMatrix(), render_world_.Snapshot());
            // Opaque-only for the deferred G-buffer; alpha-blended surfaces need
            // a forward pass (a later roadmap step), so they are skipped here.
            const SceneDrawLists draw_lists = SceneDrawListBuilder::Build(
                visible_proxies, *material_system_, *resource_resolver_, MaterialPass::GBuffer);
            for (const SceneDrawItem &item : draw_lists.opaque)
            {
                RecordMeshProxy(item.proxy, per_pass_data, *recorder, MaterialPass::GBuffer);
            }
        }
        gbuffer_target->EndRecording(*recorder);
    }

    void RenderSystem::RecordGBufferDebugViewPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const hdr_target = frame_targets_.GetTarget(RenderTargetName::SceneHdr);
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        RenderTarget *const shadow_target = frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
        if (!recorder || !hdr_target || !gbuffer_target || !shadow_target ||
            !PrepareGBufferDebugPassResources())
        {
            return;
        }
        const graphics::Extent2D extent = active_frame_context_->GetRenderExtent();
        if (extent.width == 0 || extent.height == 0 ||
            !hdr_target->BeginRecording(*recorder))
        {
            return;
        }
        (void)extent;

        // The fourth panel converts D4's sampled depth producer into linear
        // SceneHdr; raw D32 attachment bytes never escape.
        const graphics::DescriptorSetHandle debug_bindings =
            active_frame_context_->AllocateResourceBindingSet(
                gbuffer_debug_pipeline_,
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
                      0, 5, shadow_target->GetSampledDepthTexture(),
                      gbuffer_debug_sampler_}}});
        if (debug_bindings.IsValid())
        {
            recorder->BindPipeline(gbuffer_debug_pipeline_);
            recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
            recorder->BindResourceBindings(gbuffer_debug_pipeline_, debug_bindings);
            recorder->DrawIndexed();
        }
        hdr_target->EndRecording(*recorder);
    }

    void RenderSystem::RecordDeferredLightingPass()
    {
        if (!active_frame_context_ || !frame_lighting_binding_.IsValid() ||
            !pass_schedule_.IsValid())
        {
            return;
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
            !spot_shadow_target || !point_shadow_target ||
             !PrepareDeferredLightingPassResources() ||
            !hdr_target->BeginRecording(*recorder))
        {
            return;
        }

        DeferredLightingGpuData lighting_data{};
        lighting_data.inverse_view_projection =
            scene_camera_.GetViewProjectionMatrix().Inverse().Transpose();
        const Vector3f &camera_position = scene_camera_.GetPosition();
        lighting_data.camera_world_position = Vector4f{camera_position, 1.0f};
        lighting_data.environment_ibl_params = Vector4f{
            environment_ibl_enabled_ ? 1.0f : 0.0f,
            static_cast<float>(environment_prefilter_level_count_),
            environment_ibl_intensity_, 0.0f};
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
                           0, 7, environment_texture_binding_.texture,
                           environment_texture_binding_.sampler},
                       graphics::SampledTextureBinding{
                           0, 8, environment_irradiance_binding_.texture,
                           environment_irradiance_binding_.sampler},
                       graphics::SampledTextureBinding{
                           0, 9, environment_prefilter_binding_.texture,
                           environment_prefilter_binding_.sampler},
                       graphics::SampledTextureBinding{
                           0, 10, environment_brdf_lut_binding_.texture,
                           environment_brdf_lut_binding_.sampler}}});
            if (bindings.IsValid())
            {
                recorder->BindPipeline(deferred_lighting_pipeline_);
                recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
                recorder->BindResourceBindings(deferred_lighting_pipeline_, bindings);
                recorder->DrawIndexed();
            }
        }
        hdr_target->EndRecording(*recorder);
    }

    bool RenderSystem::PrepareFullscreenPassResources()
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
        gbuffer_debug_fullscreen_mesh_ = backend_->CreateMesh(fullscreen_mesh);
        gbuffer_debug_sampler_ = backend_->CreateSampler(graphics::SamplerSettings{});
        return gbuffer_debug_fullscreen_mesh_.IsValid() && gbuffer_debug_sampler_.IsValid();
    }

    bool RenderSystem::PrepareDeferredLightingPassResources()
    {
        if (deferred_lighting_pipeline_.IsValid() && directional_shadow_sampler_.IsValid() &&
            spot_shadow_sampler_.IsValid() && point_shadow_sampler_.IsValid() &&
            environment_texture_binding_.texture.IsValid() &&
            environment_texture_binding_.sampler.IsValid() &&
            environment_irradiance_binding_.texture.IsValid() &&
            environment_prefilter_binding_.texture.IsValid() &&
            environment_brdf_lut_binding_.texture.IsValid())
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
        if (!environment_texture_binding_.texture.IsValid() ||
            !environment_texture_binding_.sampler.IsValid())
        {
            data::TextureData fallback{};
            fallback.width = 1;
            fallback.height = 1;
            fallback.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
            fallback.pixels.resize(4 * sizeof(uint16_t), 0);
            environment_texture_binding_ = resource_resolver_->GetOrCreateTextureBinding(
                {}, fallback, MaterialTextureColorSpace::Linear);
        }
        if (!environment_irradiance_binding_.texture.IsValid())
        {
            data::TextureData fallback{};
            fallback.width = 1;
            fallback.height = 1;
            fallback.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
            fallback.pixels.resize(4 * sizeof(uint16_t), 0);
            environment_irradiance_binding_ = resource_resolver_->GetOrCreateTextureBinding(
                {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
                TextureCacheVariant::EnvironmentIrradiance);
            environment_prefilter_binding_ = resource_resolver_->GetOrCreateTextureBinding(
                {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
                TextureCacheVariant::EnvironmentPrefilter);
            environment_brdf_lut_binding_ = resource_resolver_->GetOrCreateTextureBinding(
                {}, fallback, MaterialTextureColorSpace::Linear, nullptr,
                TextureCacheVariant::EnvironmentBrdfLut);
        }
        if (!directional_shadow_sampler_.IsValid() || !spot_shadow_sampler_.IsValid() ||
            !point_shadow_sampler_.IsValid() ||
            !environment_texture_binding_.texture.IsValid() ||
            !environment_texture_binding_.sampler.IsValid() ||
            !environment_irradiance_binding_.texture.IsValid() ||
            !environment_prefilter_binding_.texture.IsValid() ||
            !environment_brdf_lut_binding_.texture.IsValid())
        {
            return false;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID program_id = asset_manager.LoadSync(
            GetShaderDirectory() + "deferred_lighting.shader");
        auto program = asset_manager.GetResource<asset::ShaderProgramResource>(program_id);
        if (!program)
        {
            return false;
        }
        const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
        resource_pipeline_->ProcessShader(shaders);
        const auto vert_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL);
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

    bool RenderSystem::PrepareEnvironmentIbl(asset::AssetID source_asset,
                                             const data::TextureData &source)
    {
        const std::optional<resource::EnvironmentIblData> processed =
            resource_pipeline_->ProcessEnvironmentIbl(source);
        if (!processed.has_value())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Environment IBL preprocessing requires a valid RGBA16F panorama");
            return false;
        }

        MaterialSamplerDesc panorama_sampler{};
        panorama_sampler.address_u = MaterialSamplerAddressMode::Repeat;
        panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        environment_irradiance_binding_ = resource_resolver_->GetOrCreateTextureBinding(
            source_asset, processed->irradiance, MaterialTextureColorSpace::Linear,
            &panorama_sampler, TextureCacheVariant::EnvironmentIrradiance);
        environment_prefilter_binding_ = resource_resolver_->GetOrCreateTextureBinding(
            source_asset, processed->prefiltered_radiance,
            MaterialTextureColorSpace::Linear, &panorama_sampler,
            TextureCacheVariant::EnvironmentPrefilter);

        MaterialSamplerDesc lut_sampler{};
        lut_sampler.address_u = MaterialSamplerAddressMode::ClampToEdge;
        lut_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
        lut_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
        environment_brdf_lut_binding_ = resource_resolver_->GetOrCreateTextureBinding(
            source_asset, processed->brdf_lut, MaterialTextureColorSpace::Linear,
            &lut_sampler, TextureCacheVariant::EnvironmentBrdfLut);
        environment_prefilter_level_count_ = processed->prefilter_level_count;
        environment_ibl_enabled_ =
            environment_irradiance_binding_.texture.IsValid() &&
            environment_irradiance_binding_.sampler.IsValid() &&
            environment_prefilter_binding_.texture.IsValid() &&
            environment_prefilter_binding_.sampler.IsValid() &&
            environment_brdf_lut_binding_.texture.IsValid() &&
            environment_brdf_lut_binding_.sampler.IsValid();
        return environment_ibl_enabled_;
    }

    bool RenderSystem::PrepareGBufferDebugPassResources()
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

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID program_id = asset_manager.LoadSync(
            GetShaderDirectory() + "gbuffer_debug_view.shader");
        auto program = asset_manager.GetResource<asset::ShaderProgramResource>(program_id);
        if (!program)
        {
            return false;
        }
        const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
        resource_pipeline_->ProcessShader(shaders);
        for (const asset::ShaderPtr &shader : shaders)
        {
            if (!shader || shader->status == asset::ShaderStatus::CompileFailed)
            {
                return false;
            }
        }
        const auto vert_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL);
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

    void RenderSystem::RecordToneMapPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const hdr_target = frame_targets_.GetTarget(RenderTargetName::SceneHdr);
        RenderTarget *const scene_target = frame_targets_.GetTarget(RenderTargetName::SceneColor);
        if (!recorder || !hdr_target || !scene_target || !PrepareToneMapPassResources() ||
            !scene_target->BeginRecording(*recorder))
        {
            return;
        }

        const graphics::DescriptorSetHandle tone_map_bindings =
            active_frame_context_->AllocateResourceBindingSet(
                tone_map_pipeline_,
                {0,
                 {graphics::SampledTextureBinding{
                     0, 2, hdr_target->GetColorAttachmentTexture(0),
                     gbuffer_debug_sampler_}}});
        if (tone_map_bindings.IsValid())
        {
            recorder->BindPipeline(tone_map_pipeline_);
            recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
            recorder->BindResourceBindings(tone_map_pipeline_, tone_map_bindings);
            recorder->DrawIndexed();
        }
        scene_target->EndRecording(*recorder);
    }

    void RenderSystem::RecordPendingCapturePass()
    {
        if (!render_capture_service_)
        {
            return;
        }
        const std::optional<CaptureView> pending_view =
            render_capture_service_->GetPendingView();
        if (!pending_view.has_value())
        {
            return;
        }

        if (*pending_view != CaptureView::SceneColor &&
            !RecordCaptureViewPass(*pending_view))
        {
            render_capture_service_->RejectPendingCapture(
                "Render could not record the requested capture-view conversion pass");
            return;
        }
        render_capture_service_->EnqueuePendingReadback();
    }

    bool RenderSystem::RecordCaptureViewPass(CaptureView view)
    {
        if (!active_frame_context_ || view == CaptureView::SceneColor ||
            !pass_schedule_.IsValid())
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
        output_target->EndRecording(*recorder);
        return true;
    }

    bool RenderSystem::PrepareToneMapPassResources()
    {
        if (tone_map_pipeline_.IsValid())
        {
            return true;
        }
        if (!PrepareFullscreenPassResources())
        {
            return false;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID program_id = asset_manager.LoadSync(
            GetShaderDirectory() + "tone_map.shader");
        auto program = asset_manager.GetResource<asset::ShaderProgramResource>(program_id);
        if (!program)
        {
            return false;
        }
        const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
        resource_pipeline_->ProcessShader(shaders);
        const auto vert_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL);
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
              ShaderStage::SHADER_STAGE_FRAGMENT}},
        };
        tone_map_pipeline_ = backend_->CreatePipelineResource(desc);
        return tone_map_pipeline_.IsValid();
    }

    bool RenderSystem::PrepareCaptureViewPassResources()
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

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID program_id = asset_manager.LoadSync(
            GetShaderDirectory() + "capture_view.shader");
        auto program = asset_manager.GetResource<asset::ShaderProgramResource>(program_id);
        if (!program)
        {
            return false;
        }
        const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
        resource_pipeline_->ProcessShader(shaders);
        const auto vert_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL);
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

    bool RenderSystem::PrepareDirectionalShadowPassResources()
    {
        if (directional_shadow_pipeline_.IsValid())
        {
            return true;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID program_id = asset_manager.LoadSync(
            GetShaderDirectory() + "directional_shadow_depth.shader");
        auto program = asset_manager.GetResource<asset::ShaderProgramResource>(program_id);
        if (!program)
        {
            return false;
        }
        const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
        resource_pipeline_->ProcessShader(shaders);
        const auto vert_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL);
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
            {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_VERTEX},
             {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_VERTEX}},
        };
        directional_shadow_pipeline_ = backend_->CreatePipelineResource(desc);
        return directional_shadow_pipeline_.IsValid();
    }

    void RenderSystem::RecordShadowCaster(const MeshProxy &proxy,
                                          const graphics::PerPassData &per_pass_data,
                                          graphics::CommandRecorder &recorder)
    {
        if (!proxy.flags.visible || !proxy.flags.casts_shadow || !proxy.mesh.IsValid() ||
            !directional_shadow_pipeline_.IsValid())
        {
            return;
        }
        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
        const UniformAllocation per_pass = active_frame_context_->AllocateUniform(per_pass_data);
        const UniformAllocation per_object = active_frame_context_->AllocateUniform(per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return;
        }
        const graphics::DescriptorSetHandle bindings = active_frame_context_->AllocateResourceBindingSet(
            directional_shadow_pipeline_,
            {0,
             {graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
              graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range}}});
        if (!bindings.IsValid())
        {
            return;
        }
        recorder.BindPipeline(directional_shadow_pipeline_);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(directional_shadow_pipeline_, bindings);
        recorder.DrawIndexed();
    }

    void RenderSystem::RecordMeshProxy(const MeshProxy &proxy,
                                       const graphics::PerPassData &per_pass_data,
                                       graphics::CommandRecorder &recorder,
                                       MaterialPass pass)
    {
        if (!proxy.flags.visible || !proxy.mesh.IsValid())
        {
            return;
        }

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
        const UniformAllocation per_pass = active_frame_context_->AllocateUniform(per_pass_data);
        const UniformAllocation per_object = active_frame_context_->AllocateUniform(per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return;
        }

        const std::vector<graphics::ResourceBinding> draw_bindings{
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range},
        };
        const FrameMaterialBinding material_binding = active_frame_context_->CreateMaterialBinding(
            *material_system_, *resource_resolver_, proxy.material, draw_bindings, pass);
        if (!active_frame_context_->IsMaterialBindingCurrent(material_binding))
        {
            return;
        }

        recorder.BindPipeline(material_binding.pipeline);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(material_binding.pipeline, material_binding.descriptor_set);
        recorder.DrawIndexed();
    }

    void RenderSystem::ConsumeRequests(std::size_t max_items)
    {
        if (!load_queue_)
        {
            return;
        }

        std::size_t consumed = 0;
        asset::AssetLoadRequest request;
        while ((max_items == 0 || consumed < max_items) && load_queue_->TryPop(request))
        {
            RenderCacheEntry entry;
            if (ConsumeOne(request, entry))
            {
                render_cache_[request.request_id] = std::move(entry);
            }
            ++consumed;
        }
    }

    bool RenderSystem::ConsumeOne(const asset::AssetLoadRequest &request, RenderCacheEntry &entry)
    {
        // The render cache only holds GPU-bound artifacts. Refuse the rest (audio
        // today) before LoadSync, so a non-render request never burns a render
        // frame budget decoding a file the renderer will never read.
        switch (request.type)
        {
        case asset::AssetType::KPAT_ShaderProgram:
        case asset::AssetType::KPAT_Shader:
        case asset::AssetType::KPAT_Texture:
        case asset::AssetType::KPAT_Mesh:
        case asset::AssetType::KPAT_Model:
        case asset::AssetType::KPAT_Material:
            break;
        default:
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Skipping non-render asset request (type %d): %s",
                   static_cast<int>(request.type), request.path.c_str());
            return false;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID id = asset_manager.LoadSync(request.path);
        if (!id.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Failed to load asset: %s", request.path.c_str());
            return false;
        }

        entry.asset_id = id;

        // Bake shader programs through the resource pipeline, then pin the payload
        // per type so it can't be unloaded while the renderer still holds it.
        switch (request.type)
        {
        case asset::AssetType::KPAT_ShaderProgram:
        {
            auto program = asset_manager.GetResource<asset::ShaderProgramResource>(id);
            if (!program)
            {
                return false;
            }
            // Startup shader compile is a slow, one-time pass; report it so the
            // log (and later a loading screen) can show progress. Observer fires
            // per stage before it is processed; done = already finished.
            const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
            resource_pipeline_->ProcessShader(
                shaders,
                [](resource::ShaderProcessPhase phase, int done, int total,
                   const asset::ShaderResource *shader)
                {
                    KP_LOG("RenderLog", LOG_LEVEL_INFO,
                           "Shader %d/%d: %s (phase %d)",
                           done + 1, total, shader->desc.file.c_str(),
                           static_cast<int>(phase));
                });
            const graphics::PipelineHandle pipeline =
                resource_resolver_->GetOrCreateDefaultPipeline(id, *program);
            if (!pipeline.IsValid())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "No default pipeline created for shader program: %s",
                       request.path.c_str());
                return false;
            }
            entry.resource = pipeline;
            entry.payload = std::move(program);
            return true;
        }
        case asset::AssetType::KPAT_Shader:
            entry.payload = asset_manager.GetResource<asset::ShaderResource>(id);
            return true;
        case asset::AssetType::KPAT_Texture:
        {
            auto texture = asset_manager.GetResource<asset::TextureResource>(id);
            if (!texture || !texture->data)
            {
                return false;
            }
            const bool is_environment = !bootstrap_scene_info_.environment_path.empty() &&
                                        request.path == bootstrap_scene_info_.environment_path;
            MaterialSamplerDesc panorama_sampler{};
            panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
            panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
            const TextureBinding texture_binding =
                resource_resolver_->GetOrCreateTextureBinding(
                    id, *texture->data, MaterialTextureColorSpace::Srgb,
                    is_environment ? &panorama_sampler : nullptr);
            if (is_environment)
            {
                environment_texture_binding_ = texture_binding;
                if (!PrepareEnvironmentIbl(id, *texture->data))
                {
                    return false;
                }
            }
            entry.payload = std::move(texture);
            if (!texture_binding.texture.IsValid() || !texture_binding.sampler.IsValid())
            {
                return false;
            }
            entry.resource = texture_binding;
            return true;
        }
        case asset::AssetType::KPAT_Mesh:
        {
            auto mesh = asset_manager.GetResource<asset::MeshResource>(id);
            if (!mesh || !mesh->data)
            {
                return false;
            }
            const graphics::MeshHandle mesh_handle = resource_resolver_->GetOrCreateMesh(id, *mesh->data);
            entry.payload = std::move(mesh);
            if (!mesh_handle.IsValid())
            {
                return false;
            }
            entry.resource = mesh_handle;
            return true;
        }
        case asset::AssetType::KPAT_Model:
        {
            auto model = asset_manager.GetResource<asset::ModelResource>(id);
            if (!model)
            {
                return false;
            }
            const asset::AssetID mesh_id = model->GetData(asset::ModelGeometryType::KPMG_Mesh);
            auto mesh = model->GetMesh();
            if (!mesh || !mesh->data)
            {
                return false;
            }
            const graphics::MeshHandle mesh_handle =
                resource_resolver_->GetOrCreateMesh(mesh_id, *mesh->data);
            entry.payload = std::move(model);
            if (!mesh_handle.IsValid())
            {
                return false;
            }
            entry.resource = mesh_handle;
            return true;
        }
        case asset::AssetType::KPAT_Material:
            entry.payload = asset_manager.GetResource<asset::MaterialResource>(id);
            return std::get<asset::MaterialPtr>(entry.payload) != nullptr;
        default:
            return false;
        }
    }

    RenderableSourceResolution RenderSystem::ResolveRenderableSource(
        const PrimitiveRenderableSourceDesc &source)
    {
        const auto *const static_mesh = std::get_if<StaticMeshRenderableSourceDesc>(&source);
        if (static_mesh == nullptr)
        {
            return {RenderableSourceState::Failed, "unsupported renderable source variant", std::nullopt};
        }
        if (!static_mesh->mesh_asset.IsValid() ||
            static_mesh->mesh_asset.type != asset::AssetType::KPAT_Mesh)
        {
            return {RenderableSourceState::Failed, "static mesh source has an invalid mesh asset", std::nullopt};
        }
        MaterialInstanceHandle material_instance;
        const MaterialResolution material_resolution =
            ResolveMaterialAsset(static_mesh->material_asset, material_instance);
        if (material_resolution.state == MaterialResourceState::Failed)
        {
            return {RenderableSourceState::Failed, material_resolution.diagnostic, std::nullopt};
        }
        if (material_resolution.state != MaterialResourceState::Ready)
        {
            return {RenderableSourceState::Pending, material_resolution.diagnostic, std::nullopt};
        }

        const auto mesh = asset::AssetManager::GetInstance().GetResource<asset::MeshResource>(
            static_mesh->mesh_asset);
        if (!mesh || !mesh->data)
        {
            return {RenderableSourceState::Pending, "mesh asset is not loaded", std::nullopt};
        }
        const graphics::MeshHandle mesh_handle =
            resource_resolver_->GetOrCreateMesh(static_mesh->mesh_asset, *mesh->data);
        if (!mesh_handle.IsValid())
        {
            return {RenderableSourceState::Failed, "mesh resource creation failed", std::nullopt};
        }

        MeshProxyDesc proxy_desc{};
        proxy_desc.mesh = mesh_handle;
        proxy_desc.material = material_instance;
        proxy_desc.world_transform = static_mesh->world_transform;
        proxy_desc.world_bounds = static_mesh->world_bounds;
        proxy_desc.flags = static_mesh->flags;
        proxy_desc.lod_bias = static_mesh->lod_bias;
        return {RenderableSourceState::Ready, {}, proxy_desc};
    }

    MaterialResolution RenderSystem::ResolveMaterialAsset(asset::AssetID material_asset,
                                                           MaterialInstanceHandle &out_instance)
    {
        if (!material_asset_resolver_)
        {
            return {MaterialResourceState::Pending, "material system is not initialized"};
        }
        return material_asset_resolver_->Resolve(material_asset, out_instance);
    }

    void RenderSystem::DrainRenderableSources()
    {
        auto callback = [this](const PrimitiveRenderableSourceDesc &source)
            { return ResolveRenderableSource(source); };
        source_registry_.Drain(
            render_world_, callback);
    }

    void RenderSystem::DrainLightSources()
    {
        light_source_registry_.Drain(light_world_);
    }

    void RenderSystem::DrainCameraSources()
    {
        camera_source_registry_.Drain();
        const std::optional<CameraSourceDesc> active_source =
            camera_source_registry_.GetActiveSource();
        if (!active_source.has_value())
        {
            ApplyDefaultCamera();
            return;
        }

        const CameraSourceDesc &source = *active_source;
        scene_camera_.SetPosition(source.world_transform.position_);
        scene_camera_.SetRotation(source.world_transform.rotator_);
        scene_camera_.SetProjectionMode(source.projection_mode);
        scene_camera_.SetFOV(source.field_of_view_degrees);
        scene_camera_.SetNearPlane(source.near_plane);
        scene_camera_.SetFarPlane(source.far_plane);
        scene_camera_.SetOrthographicHeight(source.orthographic_height);
    }

    const RenderCacheEntry *RenderSystem::FindCached(asset::AssetID asset_id) const
    {
        for (const auto &[request_id, entry] : render_cache_)
        {
            (void)request_id;
            if (entry.asset_id == asset_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    std::optional<StaticMeshRenderableSourceDesc> RenderSystem::TakeBootstrapRenderableSource()
    {
        if (bootstrap_renderable_sources_.empty())
        {
            return std::nullopt;
        }
        StaticMeshRenderableSourceDesc source = std::move(bootstrap_renderable_sources_.front());
        bootstrap_renderable_sources_.erase(bootstrap_renderable_sources_.begin());
        return source;
    }

    std::vector<StaticMeshRenderableSourceDesc> RenderSystem::TakeBootstrapRenderableSources()
    {
        return std::exchange(bootstrap_renderable_sources_, {});
    }

    void RenderSystem::PrepareBootstrapRenderableSources()
    {
        if ((!bootstrap_scene_info_.IsComplete() && bootstrap_scene_info_.objects.empty()) ||
            !bootstrap_renderable_sources_.empty())
        {
            return;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        std::vector<BootstrapSceneObjectInfo> objects;
        if (bootstrap_scene_info_.IsComplete())
        {
            objects.push_back({bootstrap_scene_info_.model_path,
                               bootstrap_scene_info_.material_path,
                               bootstrap_scene_info_.world_transform});
        }
        objects.insert(objects.end(), bootstrap_scene_info_.objects.begin(),
                       bootstrap_scene_info_.objects.end());

        for (const BootstrapSceneObjectInfo &object : objects)
        {
            const asset::AssetID model_asset = asset_manager.LoadSync(object.model_path);
            const asset::AssetID material_asset = asset_manager.LoadSync(object.material_path);
            const auto model = asset_manager.GetResource<asset::ModelResource>(model_asset);
            const asset::AssetID mesh_asset = model
                                                  ? model->GetData(asset::ModelGeometryType::KPMG_Mesh)
                                                  : asset::AssetID{};
            if (!mesh_asset.IsValid() || !material_asset.IsValid())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Bootstrap scene object could not prepare a mesh or material asset");
                continue;
            }

            StaticMeshRenderableSourceDesc source{};
            source.mesh_asset = mesh_asset;
            source.material_asset = material_asset;
            source.world_transform = object.world_transform;
            const auto mesh = asset_manager.GetResource<asset::MeshResource>(mesh_asset);
            if (!mesh || !mesh->data || mesh->data->vertices.empty())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Bootstrap scene object mesh has no geometry bounds");
                continue;
            }
            source.local_bounds = mesh->local_bounds;
            source.world_bounds = TransformBounds(source.local_bounds, source.world_transform);
            bootstrap_renderable_sources_.push_back(std::move(source));
        }
        KP_LOG("RenderLog", LOG_LEVEL_INFO, "Bootstrap gameplay render source(s) prepared: %zu",
               bootstrap_renderable_sources_.size());
    }

    void RenderSystem::DestroyMaterialAssetRecords()
    {
        if (material_asset_resolver_)
        {
            material_asset_resolver_->Clear();
            material_asset_resolver_.reset();
        }
    }

    void RenderSystem::ApplyDefaultCamera()
    {
        scene_camera_.SetPosition({0.f, 0.f, 300.f});
        scene_camera_.SetRotation({0.f, -90.f, 0.f});
        scene_camera_.SetProjectionMode(CameraProjectionMode::Perspective);
        scene_camera_.SetFOV(45.f);
        scene_camera_.SetNearPlane(1.f);
        scene_camera_.SetFarPlane(2000.f);
        scene_camera_.SetOrthographicHeight(10.f);
    }

    void RenderSystem::ApplyPendingSceneRenderTargetExtent()
    {
        if (!backend_ || pending_scene_render_target_extent_.width == 0 ||
            pending_scene_render_target_extent_.height == 0)
        {
            return;
        }

        const graphics::Extent2D requested = pending_scene_render_target_extent_;
        pending_scene_render_target_extent_ = {};
        // RebuildForExtent retires via WaitIdle internally only when the extent
        // changed, so a stable size keeps the shared target's GPU generations
        // intact across frames. active_frame_context_ is nulled here to match the
        // old pre-rebuild boundary; the next BeginFrame re-acquires it.
        frame_targets_.RebuildForExtent(*backend_, requested.width, requested.height);
        active_frame_context_ = nullptr;
        if (!frame_targets_.GetTarget(RenderTargetName::SceneColor)->IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Failed to resize scene render target to %u x %u", requested.width,
                   requested.height);
        }
    }

    FrameContext *RenderSystem::GetCurrentFrameContext()
    {
        if (!backend_ || !backend_->GetCommandRecorder())
        {
            return nullptr;
        }
        const uint32_t index = backend_->GetCurrentFrameIndex();
        return index < frame_contexts_.size() ? &frame_contexts_[index] : nullptr;
    }

    void RenderSystem::Shutdown()
    {
        if (!backend_)
        {
            frame_lighting_binding_ = {};
            active_directional_shadow_.reset();
            active_spot_shadow_.reset();
            active_point_shadow_.reset();
            spot_shadow_recorded_ = false;
            point_shadow_recorded_ = false;
            render_capture_service_.reset();
            camera_source_registry_.Clear();
            source_registry_.Clear(render_world_);
            render_world_.Clear();
            light_source_registry_.Clear(light_world_);
            light_world_.Clear();
            bootstrap_renderable_sources_.clear();
            DestroyMaterialAssetRecords();
            material_system_.reset();
            resource_resolver_.reset();
            return;
        }

        // Releasing material instances first retires their bindless table slots.
        // WaitIdle must follow that release so Graphics can collect the retired
        // table references before the resolver destroys cached textures/samplers.
        camera_source_registry_.Clear();
        source_registry_.Clear(render_world_);
        render_world_.ApplyPendingCommands();
        render_world_.Clear();
        light_source_registry_.Clear(light_world_);
        light_world_.Clear();
        frame_lighting_binding_ = {};
        active_directional_shadow_.reset();
        active_spot_shadow_.reset();
        active_point_shadow_.reset();
        spot_shadow_recorded_ = false;
        point_shadow_recorded_ = false;
        bootstrap_renderable_sources_.clear();
        DestroyMaterialAssetRecords();
        material_system_.reset();
        backend_->WaitIdle();
        if (graphics::IRenderTargetReadback *const readback = backend_->GetRenderTargetReadback())
        {
            readback->DrainPendingReadbacks("Render system shutdown");
        }
        render_capture_service_.reset();
        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        frame_targets_.Cleanup();
        if (gbuffer_debug_pipeline_.IsValid())
        {
            backend_->DestroyPipelineResource(gbuffer_debug_pipeline_);
        }
        if (capture_view_pipeline_.IsValid())
        {
            backend_->DestroyPipelineResource(capture_view_pipeline_);
        }
        if (deferred_lighting_pipeline_.IsValid())
        {
            backend_->DestroyPipelineResource(deferred_lighting_pipeline_);
        }
        if (gbuffer_debug_fullscreen_mesh_.IsValid())
        {
            backend_->DestroyMesh(gbuffer_debug_fullscreen_mesh_);
        }
        if (gbuffer_debug_sampler_.IsValid())
        {
            backend_->DestroySampler(gbuffer_debug_sampler_);
        }
        if (directional_shadow_sampler_.IsValid())
        {
            backend_->DestroySampler(directional_shadow_sampler_);
        }
        if (spot_shadow_sampler_.IsValid())
        {
            backend_->DestroySampler(spot_shadow_sampler_);
        }
        if (point_shadow_sampler_.IsValid())
        {
            backend_->DestroySampler(point_shadow_sampler_);
        }
        if (tone_map_pipeline_.IsValid())
        {
            backend_->DestroyPipelineResource(tone_map_pipeline_);
        }
        gbuffer_debug_pipeline_ = {};
        capture_view_pipeline_ = {};
        deferred_lighting_pipeline_ = {};
        gbuffer_debug_fullscreen_mesh_ = {};
        gbuffer_debug_sampler_ = {};
        directional_shadow_sampler_ = {};
        spot_shadow_sampler_ = {};
        point_shadow_sampler_ = {};
        environment_texture_binding_ = {};
        environment_irradiance_binding_ = {};
        environment_prefilter_binding_ = {};
        environment_brdf_lut_binding_ = {};
        environment_prefilter_level_count_ = 0;
        environment_ibl_intensity_ = 0.25f;
        environment_ibl_enabled_ = false;
        tone_map_pipeline_ = {};
        if (directional_shadow_pipeline_.IsValid())
        {
            backend_->DestroyPipelineResource(directional_shadow_pipeline_);
        }
        directional_shadow_pipeline_ = {};
        resource_resolver_->Cleanup();
        resource_resolver_.reset();
        backend_->Cleanup();
        backend_.reset();
    }
}
