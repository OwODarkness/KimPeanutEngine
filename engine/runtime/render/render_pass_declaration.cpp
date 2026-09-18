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
            "SpotShadow", "PointShadow", "CaptureOutput",
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
            }
            return false;
        }

        const std::vector<FixedRenderPassEntry> &AuthoredEntries()
        {
            static const std::vector<FixedRenderPassEntry> entries{
                {FixedRenderPassId::DirectionalShadow, "DirectionalShadowPass",
                 {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write,
                   RenderGraphUsage::DepthAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
                {FixedRenderPassId::SpotShadow, "SpotShadowPass",
                 {{RenderPassResource::SpotShadow, RenderPassAccess::Write,
                   RenderGraphUsage::DepthAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
                {FixedRenderPassId::PointShadow, "PointShadowPass",
                 {{RenderPassResource::PointShadow, RenderPassAccess::Write,
                   RenderGraphUsage::DepthAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
                {FixedRenderPassId::GBuffer, "GBufferPass",
                 {{RenderPassResource::GBuffer, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
                {FixedRenderPassId::DeferredLighting, "DeferredLightingPass",
                 {{RenderPassResource::GBuffer, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::DirectionalShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SpotShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::PointShadow, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SceneHdr, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
                {FixedRenderPassId::ToneMap, "ToneMapPass",
                 {{RenderPassResource::SceneHdr, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
                  {RenderPassResource::SceneColor, RenderPassAccess::Write,
                   RenderGraphUsage::ColorAttachment}},
                 RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
                {FixedRenderPassId::CaptureView, "CaptureViewPass",
                 // The conversion views are derived from the G-buffer and the
                 // shadow maps; this pass does not read SceneColor, and the
                 // declaration must not claim a read that never reaches the GPU.
                 {{RenderPassResource::GBuffer, RenderPassAccess::Read,
                   RenderGraphUsage::Sampled},
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
        for (std::size_t resource_index = 0; resource_index < kResourceCount; ++resource_index)
        {
            // SceneHdr is the one resource the graph plans but does not own: its
            // contents never survive the frame, so the renderer takes it from the
            // Graphics-owned pool for the window the plan computes.
            const bool pooled =
                resource_index == static_cast<std::size_t>(RenderPassResource::SceneHdr);
            resources[resource_index] = graph.CreateTexture(
                kResourceNames[resource_index],
                pooled ? std::optional<uint64_t>(
                             static_cast<uint64_t>(RenderFrameTransient::SceneHdr))
                       : std::nullopt);
        }

        RenderGraphPassRef terminal_pass;
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
                                    owner == RenderGraphPassOwner::External, owner,
                                    entry.terminal, static_cast<uint64_t>(entry.id)});
            for (const RenderPassResourceUse &use : entry.resources)
            {
                const GraphTextureHandle &resource =
                    resources[static_cast<std::size_t>(use.resource)];
                if (use.access == RenderPassAccess::Read)
                {
                    pass.Read(resource, use.usage);
                }
                else
                {
                    pass.Write(resource, use.usage);
                }
            }
            if (owner == RenderGraphPassOwner::External)
            {
                terminal_pass = pass;
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
