#ifndef KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H
#define KPENGINE_RUNTIME_RENDER_DEFERRED_RENDERER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "graphics/backend/common/render_backend.h"
#include "render_capture_service.h"
#include "frame_context.h"
#include "render/light/light_world.h"
#include "render/material/material_system.h"
#include "render_camera.h"
#include "render_graph/render_graph_frame.h"
#include "render_graph/render_graph_bindings.h"
#include "render_graph/render_graph_executor.h"
#include "render_pass_declaration.h"
#include "render/passes/scene_draw_recorder.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/passes/deferred_lighting_pass.h"
#include "render/passes/screen_space_ao_pass.h"
#include "render/passes/tone_map_pass.h"
#include "render/passes/capture_view_pass.h"
#include "render/passes/path_tracing_pass.h"
#include "render/passes/shadow_pass.h"
#include "prepared_render_asset_catalog.h"
#include "render_world/render_world.h"
#include "renderer_frame_targets.h"
#include "render_profile.h"
#include "path_trace_adaptive_spp.h"
#include "path_trace_settings.h"
#include "ray_tracing/ray_tracing_scene.h"

namespace kpengine::render
{
    class RenderResourceResolver;

    struct DeferredRendererInitInfo
    {
        graphics::RenderBackend &backend;
        RenderResourceResolver &resource_resolver;
        MaterialSystem &materials;
        const PreparedRenderAssetCatalog &prepared_assets;
        bool ray_tracing_enabled = true;
        bool path_tracing_enabled = true;
    };

    struct DeferredRendererInitResult
    {
        bool success = false;
        std::string diagnostic;

        explicit operator bool() const { return success; }
    };

    struct RenderSceneFrameInput;

    struct DeferredRendererFrameResult
    {
        bool normal_recording_completed = true;
        bool capture_target_ready = false;
    };

    // Concrete owner of deferred render policy, pass-private state, and the
    // logical targets used by one RenderSystem frame bracket.
    class DeferredRenderer final
    {
    public:
        DeferredRenderer() = default;
        ~DeferredRenderer();
        DeferredRenderer(const DeferredRenderer &) = delete;
        DeferredRenderer &operator=(const DeferredRenderer &) = delete;

        DeferredRendererInitResult Initialize(const DeferredRendererInitInfo &info,
                                              uint32_t width, uint32_t height);
        void Cleanup();

        void RequestExtent(uint32_t width, uint32_t height);
        void SetPathTraceSettings(const PathTraceSettings &settings);
        void SetScreenSpaceAoSettings(ScreenSpaceAoSettings settings);
        void InjectNextPathTraceDispatchFailure();
        void InvalidateRayTracingTextureBindings();
        void ApplyPendingExtent();
        const RenderTarget &GetSceneRenderTarget() const;
        spatial::Ray BuildSceneRay(float ndc_x, float ndc_y,
                                   float viewport_aspect) const;
        std::optional<Vector3f> ProjectScenePoint(const Vector3f &world_point,
                                                   float viewport_aspect) const;
        graphics::RenderTargetView GetViewportRenderTargetView(CaptureView view) const;
        graphics::RenderTargetHandle GetCaptureTarget(CaptureView view) const;
        uint64_t GetTriangleCount() const { return triangle_count_; }
        RenderProfileSnapshot GetProfileSnapshot() const { return profile_; }
        bool IsFramePlanValid() const { return frame_plan_valid_; }

        bool ExecuteEditorCompositePass(const std::function<void()> &record_pass);
        bool FinalizeFrame();

        DeferredRendererFrameResult RecordFrame(
            FrameContext &frame_context, const RenderSceneFrameInput &input);

    private:
        // Applies the state requirements the plan records for one pass, before
        // the pass records anything. The plan owns what state a resource must be
        // in; the backend owns what it is currently in and elides what is
        // already satisfied.
        // The target a pass records into, named by its write use. Null for a pass
        // that writes no attachment, such as the external terminal.
        RenderTarget *ResolveFrameTexture(
            RenderFrameResourceRole role,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        void ClearActiveFrameInputs();
        bool BuildFrameResourceBindings(const CompiledRenderFramePlan &plan);
        void CancelPreparedFrameBuildResources();
        // The description for a declared transient key, or null for one this
        // renderer does not implement.
        std::optional<graphics::RenderTargetDesc> DescribeFrameTransient(
            uint64_t key, const graphics::Extent2D &extent) const;
        void ConfigureFramePlans();
        const CompiledRenderFramePlan *GetCompiledFramePlan(
            RenderFrameConditions conditions) const;
        const CompiledRenderGraph *GetFramePlan(RenderFrameConditions conditions) const;
        bool RecordDirectionalShadowPass();
        bool RecordSpotShadowPass();
        bool RecordPointShadowPass();
        bool RecordGBufferPass();
        bool RecordScreenSpaceAoEstimatePass();
        bool RecordScreenSpaceAoFilterPass();
        bool RecordDeferredLightingPass();
        bool RecordToneMapPass();
        bool RecordRayTracingPathTracePass();
        bool RecordCaptureViewPass(CaptureView view, RenderTargetName output_target);
        bool ExecutePass(FixedRenderPassId id, const std::vector<Light> &lights,
                         const RenderGraphPassContext &context);
        bool PrepareDeferredLightingPassResources();
        void ApplyPendingSceneRenderTargetExtent();
        void AddProfileDraws(uint64_t draw_calls, uint64_t sections);

        graphics::RenderBackend *backend_ = nullptr;
        RenderResourceResolver *resource_resolver_ = nullptr;
        MaterialSystem *material_system_ = nullptr;
        const PreparedRenderAssetCatalog *prepared_assets_ = nullptr;
        RendererFrameTargets frame_targets_;
        // One compiled plan per frame-start condition set, each compiled once.
        // The compiled plan is now the only authority for pass order.
        std::array<std::optional<CompiledRenderFramePlan>, 256> frame_plans_;
        RenderGraphExecutor graph_executor_;
        // The frame's pooled transient, wrapped around a pool-owned handle.
        // RenderTarget is not movable, so the wrapper is held by pointer.
        PathTraceSettings requested_path_trace_settings_{};
        PathTraceSettings effective_path_trace_settings_{};
        detail::PathTraceAdaptiveSppState adaptive_spp_state_{};
        std::optional<detail::PathTraceAdaptiveSppDecision>
            pending_adaptive_spp_decision_;
        bool path_trace_adaptive_sampling_active_ = false;
        std::string path_trace_sampling_state_ = "fixed";
        std::string path_trace_settings_fallback_reason_;
        RenderGraphBindings frame_bindings_;
        bool frame_execution_failed_ = false;
        bool frame_plan_valid_ = false;
        double frame_plan_compile_ms_ = 0.0;
        graphics::Extent2D pending_scene_render_target_extent_;
        FrameContext *active_frame_context_ = nullptr;
        const RenderWorld *render_world_ = nullptr;
        SceneDrawRecorder scene_draw_recorder_;
        FullscreenPassResources fullscreen_pass_resources_;
        DeferredLightingPass deferred_lighting_pass_;
        ScreenSpaceAoPass screen_space_ao_pass_;
        ScreenSpaceAoSettings screen_space_ao_settings_{};
        ShadowPass shadow_pass_;
        ToneMapPass tone_map_pass_;
        CaptureViewPass capture_view_pass_;
        PathTracingPass path_tracing_pass_;
        RayTracingScene ray_tracing_scene_;
        uint64_t triangle_count_ = 0;
        RenderCamera scene_camera_;
        std::optional<CaptureView> active_pending_capture_;
        std::optional<CaptureView> active_debug_view_;
        bool ray_tracing_enabled_ = true;
        bool path_tracing_enabled_ = true;
        RenderProfileSnapshot profile_;
        RenderGraphFailureSnapshot last_required_graph_failure_;
        std::optional<size_t> active_profile_pass_;
        const RenderGraphPassContext *active_pass_context_ = nullptr;
    };
}

#endif
