#ifndef KPENGINE_RUNTIME_RENDER_CAPTURE_SERVICE_H
#define KPENGINE_RUNTIME_RENDER_CAPTURE_SERVICE_H

#include <cstdint>
#include <functional>
#include <string>

#include "graphics/backend/common/render_target_readback.h"

namespace kpengine::render
{
    // Render-semantic capture outputs. Internal attachments are converted to a
    // stable RGBA8 color target before the common readback seam is invoked.
    //
    // The values resolve two different ways and an owner must not confuse them.
    // A conversion view is recorded by the deferred renderer's own CaptureView
    // pass. A host view is resolved by whoever owns the capture service through
    // its capture-target resolver, so Render never learns what produced it.
    enum class CaptureView : uint8_t
    {
        // The scene color attachment, read back directly without conversion.
        SceneColor,
        LinearDepth,
        WorldNormal,
        BaseColor,
        MaterialParams,
        ShadowVisibility,
        SpotShadowDepth,
        SpotShadowVisibility,
        PointShadowDepth,
        PointShadowVisibility,
        SelectionMask,
        // The standalone host's own output target, supplied by that host.
        HostOutput,
        // The engine window's presented contents, resolved by the window
        // capture path.
        EngineWindow,
    };

    // True only for the views the deferred renderer produces in its own
    // CaptureView pass. Stated positively so that adding a host-resolved view
    // cannot silently inherit deferred capture behavior.
    //
    // EngineWindow is not one of these: the window capture path resolves it,
    // and it cannot reach the deferred renderer regardless, because
    // RenderCaptureService::GetPendingView() reports no pending view for it and
    // RenderSystem::SetDebugView() refuses it.
    inline constexpr bool RequiresCaptureViewConversionPass(CaptureView view) noexcept
    {
        switch (view)
        {
        case CaptureView::LinearDepth:
        case CaptureView::WorldNormal:
        case CaptureView::BaseColor:
        case CaptureView::MaterialParams:
        case CaptureView::ShadowVisibility:
        case CaptureView::SpotShadowDepth:
        case CaptureView::SpotShadowVisibility:
        case CaptureView::PointShadowDepth:
        case CaptureView::PointShadowVisibility:
        case CaptureView::SelectionMask:
            return true;
        case CaptureView::SceneColor:
        case CaptureView::HostOutput:
        case CaptureView::EngineWindow:
            return false;
        }
        return false;
    }

    struct CaptureRequest
    {
        CaptureView view = CaptureView::SceneColor;
    };

    // Render keeps the semantic name used by the tooling API, while Graphics
    // owns the API-neutral CPU readback representation and its validation.
    using CapturedImage = graphics::CapturedImage;

    enum class CaptureResultStatus : uint8_t
    {
        Captured,
        Unavailable,
        Cancelled,
        Failed,
    };

    struct CaptureResult
    {
        CaptureResultStatus status = CaptureResultStatus::Failed;
        CapturedImage image;
        std::string diagnostic;

        bool IsSuccess() const { return status == CaptureResultStatus::Captured; }
    };

    using CapturedImageCallback = std::function<void(CaptureResult)>;

    // Public Runtime/tooling -> Render boundary. A successful request is
    // completed exactly once by its callback; no caller-visible job handle or
    // native graphics resource escapes through this interface.
    class IRenderCaptureService
    {
    public:
        virtual ~IRenderCaptureService() = default;

        virtual bool RequestCapture(CaptureRequest request,
                                    CapturedImageCallback on_completed) = 0;
    };
}

#endif
