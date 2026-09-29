#ifndef KPENGINE_RUNTIME_RENDER_PASSES_PATH_TRACING_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_PATH_TRACING_PASS_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/backend/common/render_backend.h"
#include "render/environment_frame_bindings.h"
#include "render/frame_context.h"
#include "render/path_trace_settings.h"
#include "render/passes/tone_map_pass.h"
#include "render/ray_tracing/ray_tracing_scene_view.h"
#include "render/ray_tracing/path_tracing_scene_data.h"
#include "render/render_graph/render_graph_executor.h"
#include "render/render_target.h"
#include "render/render_camera.h"

namespace kpengine::render
{
    class PreparedRenderAssetCatalog;

    class PathTracingPass final
    {
    public:
        PathTracingPass() = default;
        ~PathTracingPass() = default;
        PathTracingPass(const PathTracingPass &) = delete;
        PathTracingPass &operator=(const PathTracingPass &) = delete;

        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets);
        bool EnsureHistoryTargets(graphics::RenderBackend &backend,
                                  uint32_t width, uint32_t height);
        void ReleaseBindings(graphics::RenderBackend &backend);
        void Cleanup(graphics::RenderBackend &backend);

        uint64_t HistorySignature(
            uint32_t width, uint32_t height,
            const ray_tracing::RayTracingSceneView &scene,
            const EnvironmentFrameBindings &environment,
            const RenderCamera &camera, const PathTraceSettings &settings,
            graphics::PipelineHandle output_pipeline,
            uint64_t output_shader_signature,
            ToneMapOutputPolicy output_policy) const;
        const char *UpdateHistorySignature(uint64_t signature) noexcept;
        void CommitFrame(bool finalized, bool frame_execution_failed,
                         bool required_pass_failed, uint32_t samples_per_dispatch) noexcept;
        bool CanTraceScene(const ray_tracing::RayTracingSceneView &scene) const noexcept;
        bool ShouldReportSceneCapacity(uint64_t signature) noexcept;
        void ClearSceneCapacityReport() noexcept { scene_capacity_report_signature_ = 0; }
        uint32_t MaximumSceneRecords() const noexcept;

        bool Record(
            graphics::RenderBackend &backend, FrameContext &frame_context,
            RenderCamera &camera, const PathTraceSettings &settings,
            const ray_tracing::RayTracingSceneView &scene,
            const EnvironmentFrameBindings &environment,
            const RenderGraphPassContext &pass_context);

        void InjectNextDispatchFailure() noexcept { fail_next_dispatch_ = true; }
        bool Available() const noexcept { return pipeline_.IsValid(); }
        bool Active() const noexcept { return active_; }
        void SetActive(bool active) noexcept { active_ = active; }
        graphics::RayTracingPipelineHandle Pipeline() const noexcept { return pipeline_; }
        uint64_t ShaderSignature() const noexcept { return shader_signature_; }
        uint32_t SampleCount() const noexcept { return sample_count_; }
        uint32_t WriteIndex() const noexcept { return write_index_; }
        uint64_t CurrentHistorySignature() const noexcept { return history_signature_; }
        RenderTarget *HistoryTarget(uint32_t index) noexcept;
        const RenderTarget *HistoryTarget(uint32_t index) const noexcept;
        RenderTarget *GuideTarget() noexcept { return guide_target_.get(); }
        uint32_t GetHistoryTargetCount() const noexcept;

    private:
        struct BindingCache
        {
            graphics::DescriptorSetHandle descriptor_set;
            graphics::RayTracingPipelineHandle pipeline;
            graphics::AccelerationStructureHandle top_level;
            graphics::RayTracingBufferReferenceTableHandle scene_table;
            graphics::TextureHandle hdr_output;
            graphics::TextureHandle history_output;
            graphics::TextureHandle guide_output;
            graphics::TextureHandle environment;
            graphics::SamplerHandle environment_sampler;
            UniformAllocation camera_uniform;
        };

        void ReleaseBindings(graphics::RayTracingResourceOwner *owner);

        graphics::RayTracingPipelineHandle pipeline_;
        uint64_t shader_signature_ = 0;
        std::array<std::unique_ptr<RenderTarget>, 2> history_targets_;
        std::unique_ptr<RenderTarget> guide_target_;
        std::vector<std::array<BindingCache, 2>> bindings_;
        uint32_t write_index_ = 0;
        uint32_t sample_count_ = 0;
        uint64_t history_signature_ = 0;
        bool active_ = false;
        bool fail_next_dispatch_ = false;
        uint64_t scene_capacity_report_signature_ = 0;
    };
}

#endif
