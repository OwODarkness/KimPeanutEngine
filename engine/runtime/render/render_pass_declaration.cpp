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
            resources[resource_index] = graph.CreateTexture(kResourceNames[resource_index]);
        }

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
                    pass.Read(resource);
                }
                else
                {
                    pass.Write(resource);
                }
            }
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
