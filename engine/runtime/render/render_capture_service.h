#ifndef KPENGINE_RUNTIME_RENDER_CAPTURE_SERVICE_H
#define KPENGINE_RUNTIME_RENDER_CAPTURE_SERVICE_H

#include <cstdint>
#include <functional>
#include <string>

#include "graphics/backend/common/render_target_readback.h"

namespace kpengine::render
{
    // Render-semantic diagnostic outputs. Only SceneColor has a producer in the
    // initial capture slice; callers receive Unavailable for the reserved views.
    enum class CaptureView : uint8_t
    {
        SceneColor,
        LinearDepth,
        WorldNormal,
        BaseColor,
        MaterialParams,
        ShadowVisibility,
    };

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
