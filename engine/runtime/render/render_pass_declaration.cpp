#include "render_pass_declaration.h"

#include <array>
#include <cstddef>
#include <utility>

namespace kpengine::render
{
    namespace
    {
        constexpr std::size_t kResourceCount =
            static_cast<std::size_t>(RenderPassResource::Count);

        // Indexed by RenderPassResource. This is the only authored copy of the
        // raster frame's logical resource names.
        constexpr std::array<const char *, kResourceCount> kResourceNames{
            "SceneColor", "SceneHdr",    "GBuffer",   "DirectionalShadow",
            "SpotShadow", "PointShadow", "CaptureOutput", "PathTraceHistory",
            "PathTraceGuide",
        };

        bool IsPassEnabled(const FixedRenderPassEntry &entry, RenderFrameConditions conditions)
        {
            switch (entry.condition)
            {
            case RenderPassCondition::Always:
                return true;
            case RenderPassCondition::DiagnosticCaptureRequested:
                return conditions.diagnostic_capture;
            case RenderPassCondition::ExternalRequest:
                // Whether the Editor terminal runs is not known when the frame
                // declares, so it is always compiled and the executor decides.
                return true;
            case RenderPassCondition::RayTracingBuildRequested:
                return entry.id == FixedRenderPassId::RayTracingBlasBuild
                           ? conditions.ray_tracing_blas_build
                           : conditions.ray_tracing_tlas_build;
            case RenderPassCondition::RayTracingPathTrace:
                return conditions.ray_tracing_path_trace;
            case RenderPassCondition::RasterDiagnostic:
                return !conditions.ray_tracing_path_trace || conditions.diagnostic_capture;
            case RenderPassCondition::RasterFrame:
                return !conditions.ray_tracing_path_trace;
            }
            return false;
        }

        const std::vector<FixedRenderPassEntry> &AuthoredEntries()
        {
            static const std::vector<FixedRenderPassEntry> entries{
                {FixedRenderPassId::DirectionalShadow, "DirectionalShadowPass",
                 {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write,
                   RenderGraphUsage::DepthAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RasterDiagnostic, false},
                {FixedRenderPassId::SpotShadow, "SpotShadowPass",
                 {{RenderPassResource::SpotShadow, RenderPassAccess::Write,
                   RenderGraphUsage::DepthAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RasterDiagnostic, false},
                {FixedRenderPassId::PointShadow, "PointShadowPass",
                 {{RenderPassResource::PointShadow, RenderPassAccess::Write,
                   RenderGraphUsage::DepthAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RasterDiagnostic, false},
                {FixedRenderPassId::GBuffer, "GBufferPass",
                 // Writes all four colour attachments and the depth.
                 {{RenderPassResource::GBuffer, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment, RenderGraphAttachmentScope::Whole()}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RasterDiagnostic, false},
                {FixedRenderPassId::DeferredLighting, "DeferredLightingPass",
                 // Colours 0-2 and the sampled depth. This pass does not read
                 // the selection mask in attachment 3, and saying otherwise
                 // would transition an attachment nothing here touches.
                 {{RenderPassResource::GBuffer, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled, RenderGraphAttachmentScope::Colors(0b0111U, true)},
                  {RenderPassResource::DirectionalShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SpotShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::PointShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SceneHdr, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RasterFrame, false},
                {FixedRenderPassId::RayTracingPathTrace, "RayTracingPathTracePass",
                 {{RenderPassResource::PathTraceHistory, RenderPassAccess::Read,
                   RenderGraphUsage::StorageRead},
                  {RenderPassResource::SceneHdr, RenderPassAccess::Write,
                   RenderGraphUsage::StorageWrite},
                  {RenderPassResource::PathTraceGuide, RenderPassAccess::Write,
                   RenderGraphUsage::StorageWrite}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RayTracingPathTrace,
                 false},
                {FixedRenderPassId::ToneMap, "ToneMapPass",
                 {{RenderPassResource::SceneHdr, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  // The selection mask. This read was unstated until subresource
                  // scopes made it matter: it worked only because deferred
                  // lighting had already moved the whole G-buffer, so narrowing
                  // that requirement would have left this attachment behind.
                  {RenderPassResource::GBuffer, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled, RenderGraphAttachmentScope::Colors(0b1000U)},
                 {RenderPassResource::SceneColor, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RasterFrame, false},
                {FixedRenderPassId::RayTracingToneMap, "RayTracingToneMapPass",
                {{RenderPassResource::SceneHdr, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::PathTraceGuide, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SceneColor, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RayTracingPathTrace,
                 false},
                {FixedRenderPassId::CaptureView, "CaptureViewPass",
                 // The conversion views are derived from the G-buffer and the
                 // shadow maps; this pass does not read SceneColor, and the
                 // declaration must not claim a read that never reaches the GPU.
                 {{RenderPassResource::GBuffer, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled, RenderGraphAttachmentScope::Colors(0b1111U, true)},
                  {RenderPassResource::DirectionalShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SpotShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::PointShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::CaptureOutput, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer,
                 RenderPassCondition::DiagnosticCaptureRequested, false},
                {FixedRenderPassId::EditorComposite, "EditorCompositePass",
                 {{RenderPassResource::SceneColor, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled}},
                 RenderPassExecutionOwner::External, RenderPassCondition::ExternalRequest, true},
                {FixedRenderPassId::RayTracingBlasBuild, "RayTracingBlasBuildPass", {},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RayTracingBuildRequested,
                 false},
                {FixedRenderPassId::RayTracingTlasBuild, "RayTracingTlasBuildPass", {},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::RayTracingBuildRequested,
                 false},
            };
            return entries;
        }
    }

    const std::vector<FixedRenderPassEntry> &GetRenderFramePassEntries()
    {
        return AuthoredEntries();
    }

    RenderGraphCompileResult CompileRenderFrameGraph(RenderFrameConditions conditions)
    {
        RenderGraphBuilder graph;
        std::array<GraphTextureHandle, kResourceCount> resources{};
        const bool needs_blas = conditions.ray_tracing_blas_build;
        const bool needs_tlas = conditions.ray_tracing_tlas_build || conditions.ray_query_shadow ||
                                conditions.ray_tracing_path_trace;
        const std::optional<GraphBufferHandle> scene_geometry =
            needs_blas ? std::optional<GraphBufferHandle>(graph.ImportBuffer("SceneGeometry"))
                       : std::nullopt;
        const std::optional<GraphBufferHandle> scene_instances =
            needs_tlas ? std::optional<GraphBufferHandle>(graph.ImportBuffer("SceneInstances"))
                       : std::nullopt;
        const std::optional<GraphBufferHandle> scene_scratch =
            needs_tlas || needs_blas
                ? std::optional<GraphBufferHandle>(graph.ImportBuffer("SceneScratch"))
                : std::nullopt;
        const std::optional<GraphAccelerationStructureHandle> scene_blas =
            needs_tlas || needs_blas
                ? std::optional<GraphAccelerationStructureHandle>(
                      graph.ImportAccelerationStructure("SceneBLAS"))
                : std::nullopt;
        const std::optional<GraphAccelerationStructureHandle> scene_tlas =
            needs_tlas
                ? std::optional<GraphAccelerationStructureHandle>(
                      graph.ImportAccelerationStructure("SceneTLAS"))
                : std::nullopt;
        for (std::size_t resource_index = 0; resource_index < kResourceCount; ++resource_index)
        {
            // SceneHdr is the one resource the graph plans but does not own: its
            // contents never survive the frame, so the renderer takes it from the
            // Graphics-owned pool for the window the plan computes.
            const bool pooled =
                resource_index == static_cast<std::size_t>(RenderPassResource::SceneHdr);
            if (conditions.ray_tracing_path_trace &&
                (resource_index == static_cast<std::size_t>(RenderPassResource::SceneHdr) ||
                 resource_index == static_cast<std::size_t>(RenderPassResource::PathTraceHistory) ||
                 resource_index == static_cast<std::size_t>(RenderPassResource::PathTraceGuide)))
                resources[resource_index] = graph.ImportTexture(kResourceNames[resource_index]);
            else
                resources[resource_index] = graph.CreateTexture(
                    kResourceNames[resource_index],
                    pooled ? std::optional<uint64_t>(
                                 static_cast<uint64_t>(RenderFrameTransient::SceneHdr))
                           : std::nullopt);
        }

        RenderGraphPassRef terminal_pass;
        RenderGraphPassRef deferred_lighting_pass;
        RenderGraphPassRef blas_build_pass;
        RenderGraphPassRef tlas_build_pass;
        RenderGraphPassRef path_trace_pass;
        for (const FixedRenderPassEntry &entry : AuthoredEntries())
        {
            const RenderGraphPassCondition condition =
                entry.condition == RenderPassCondition::Always
                    ? RenderGraphPassCondition::Always
                    : RenderGraphPassCondition::Optional;
            const RenderGraphPassOwner owner =
                entry.owner == RenderPassExecutionOwner::External
                    ? RenderGraphPassOwner::External
                    : RenderGraphPassOwner::Renderer;
            // Reads and writes act on each resource's current version, so the
            // chain carries the SSA lineage without threading handles by hand.
            RenderGraphPassRef pass = graph.AddPass(
                RenderGraphPassDesc{entry.name, condition, IsPassEnabled(entry, conditions),
                                    owner == RenderGraphPassOwner::External ||
                                        entry.id == FixedRenderPassId::RayTracingBlasBuild ||
                                        entry.id == FixedRenderPassId::RayTracingTlasBuild,
                                    owner,
                                    entry.terminal, static_cast<uint64_t>(entry.id),
                                    entry.id == FixedRenderPassId::CaptureView
                                        ? RenderGraphPassFailurePolicy::Optional
                                        : RenderGraphPassFailurePolicy::Required});
            if (!IsPassEnabled(entry, conditions))
            {
                continue;
            }
            for (const RenderPassResourceUse &use : entry.resources)
            {
                const GraphTextureHandle &resource =
                    resources[static_cast<std::size_t>(use.resource)];
                if (use.access == RenderPassAccess::Read)
                {
                    pass.Read(resource, use.usage, use.scope);
                }
                else
                {
                    pass.Write(resource, use.usage, RenderGraphAttachmentOp::None, use.scope);
                }
            }
            if (entry.id == FixedRenderPassId::DeferredLighting)
            {
                deferred_lighting_pass = pass;
            }
            if (entry.id == FixedRenderPassId::RayTracingBlasBuild)
            {
                blas_build_pass = pass;
            }
            if (entry.id == FixedRenderPassId::RayTracingTlasBuild)
            {
                tlas_build_pass = pass;
            }
            if (entry.id == FixedRenderPassId::RayTracingPathTrace)
            {
                path_trace_pass = pass;
            }
            if (owner == RenderGraphPassOwner::External)
            {
                terminal_pass = pass;
            }
        }

        if (needs_blas && scene_geometry.has_value() && scene_blas.has_value() &&
            blas_build_pass.IsValid())
        {
            blas_build_pass.Read(*scene_geometry,
                                 RenderGraphUsage::AccelerationStructureBuildInput)
                .Write(*scene_blas, RenderGraphUsage::AccelerationStructureBuildOutput);
        }
        if (conditions.ray_tracing_tlas_build && scene_instances.has_value() &&
            scene_scratch.has_value() && scene_blas.has_value() && scene_tlas.has_value() &&
            tlas_build_pass.IsValid())
        {
            tlas_build_pass.Read(graph.CurrentVersion(*scene_blas),
                                 RenderGraphUsage::AccelerationStructureBuildInput)
                .Read(*scene_instances, RenderGraphUsage::AccelerationStructureBuildInput)
                .Read(*scene_scratch, RenderGraphUsage::StorageRead)
                .Write(*scene_tlas, RenderGraphUsage::AccelerationStructureBuildOutput);
            if (blas_build_pass.IsValid() && conditions.ray_tracing_blas_build)
            {
                tlas_build_pass.DependsOn(blas_build_pass);
            }
        }

        // Build passes are authored after the external terminal. When no
        // consumer reads the built AS this edge keeps the terminal last.
        if (!conditions.ray_query_shadow && terminal_pass.IsValid())
        {
            if (conditions.ray_tracing_tlas_build && tlas_build_pass.IsValid())
            {
                terminal_pass.DependsOn(tlas_build_pass);
            }
            else if (conditions.ray_tracing_blas_build && blas_build_pass.IsValid())
            {
                terminal_pass.DependsOn(blas_build_pass);
            }
        }

        if (conditions.ray_query_shadow && scene_tlas.has_value() &&
            deferred_lighting_pass.IsValid())
        {
            deferred_lighting_pass.Read(graph.CurrentVersion(*scene_tlas),
                                        RenderGraphUsage::AccelerationStructureRead,
                                        RenderGraphStage::RayTracingShader);
        }
        if (conditions.ray_tracing_path_trace && scene_tlas.has_value() &&
            path_trace_pass.IsValid())
        {
            path_trace_pass.Read(graph.CurrentVersion(*scene_tlas),
                                 RenderGraphUsage::AccelerationStructureRead,
                                 RenderGraphStage::RayTracingShader);
            if (tlas_build_pass.IsValid() && conditions.ray_tracing_tlas_build)
            {
                path_trace_pass.DependsOn(tlas_build_pass);
            }
        }

        // The host samples the conversion output through the editor viewport
        // whenever a diagnostic view is active -- GetViewportRenderTargetView
        // maps every non-SceneColor view to CaptureOutput. No renderer pass reads
        // that target, so without declaring the host's read here nothing
        // transitions it out of the attachment layout the capture pass wrote it
        // in, and the host samples it in the wrong layout.
        if (conditions.diagnostic_capture && terminal_pass.IsValid())
        {
            terminal_pass.Read(
                resources[static_cast<std::size_t>(RenderPassResource::CaptureOutput)],
                RenderGraphUsage::Sampled);
        }

        graph.ExportTexture(
            graph.CurrentVersion(resources[static_cast<std::size_t>(RenderPassResource::SceneColor)]),
            "SceneColor");
        if (conditions.diagnostic_capture)
        {
            graph.ExportTexture(
                graph.CurrentVersion(
                    resources[static_cast<std::size_t>(RenderPassResource::CaptureOutput)]),
                "CaptureOutput");
        }
        return graph.Compile();
    }
}
