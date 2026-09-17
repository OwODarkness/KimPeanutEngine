#ifndef KPENGINE_RUNTIME_RENDER_RENDER_PASS_DECLARATION_H
#define KPENGINE_RUNTIME_RENDER_RENDER_PASS_DECLARATION_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "render_graph/render_graph.h"

namespace kpengine::render
{
    enum class RenderPassResource : uint8_t
    {
        SceneColor,
        SceneHdr,
        GBuffer,
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        CaptureOutput,
        Count,
    };

    enum class RenderPassAccess : uint8_t
    {
        Read,
        Write,
    };

    enum class FixedRenderPassId : uint8_t
    {
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        GBuffer,
        DeferredLighting,
        ToneMap,
        CaptureView,
        EditorComposite,
        Count,
    };

    enum class RenderPassExecutionOwner : uint8_t
    {
        Renderer,
        External,
    };

    enum class RenderPassCondition : uint8_t
    {
        Always,
        DiagnosticCaptureRequested,
        ExternalRequest,
    };

    struct RenderPassResourceUse
    {
        RenderPassResource resource = RenderPassResource::SceneColor;
        RenderPassAccess access = RenderPassAccess::Read;
    };

    struct FixedRenderPassEntry
    {
        FixedRenderPassId id = FixedRenderPassId::DirectionalShadow;
        std::string name;
        std::vector<RenderPassResourceUse> resources;
        RenderPassExecutionOwner owner = RenderPassExecutionOwner::Renderer;
        RenderPassCondition condition = RenderPassCondition::Always;
        bool terminal = false;
    };

    // The frame-start condition set. It is resolved once, before any pass runs,
    // and does not change while the frame is in flight.
    struct RenderFrameConditions
    {
        bool diagnostic_capture = false;

        friend bool operator==(const RenderFrameConditions &,
                               const RenderFrameConditions &) = default;
    };

    // The one authored raster declaration. The compiled plan is derived from it
    // and is the only authority for pass order and resource flow.
    const std::vector<FixedRenderPassEntry> &GetRenderFramePassEntries();

    // Declares the authored passes into a compiled graph for these conditions.
    // A disabled optional pass is culled, so its key is absent from the plan.
    RenderGraphCompileResult CompileRenderFrameGraph(RenderFrameConditions conditions);
}

#endif
