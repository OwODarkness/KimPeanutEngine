#ifndef KPENGINE_RUNTIME_SCREENSHOT_SERVICE_H
#define KPENGINE_RUNTIME_SCREENSHOT_SERVICE_H

#include <cstdint>
#include <functional>
#include <string>

#include "render/render_capture_service.h"

namespace kpengine::runtime
{
    struct ScreenshotRequest
    {
        render::CaptureRequest capture;
        // Empty selects a UTC-named output under save/screenshots/. Explicit
        // paths are restricted to save/screenshots/validation/.
        std::string output_path;
    };

    enum class ScreenshotResultStatus : uint8_t
    {
        Exported,
        InvalidOutputPath,
        CaptureRejected,
        CaptureUnavailable,
        CaptureCancelled,
        CaptureFailed,
        WriteFailed,
    };

    struct ScreenshotResult
    {
        ScreenshotResultStatus status = ScreenshotResultStatus::WriteFailed;
        std::string output_path;
        std::string diagnostic;

        bool IsSuccess() const { return status == ScreenshotResultStatus::Exported; }
    };

    using ScreenshotCallback = std::function<void(ScreenshotResult)>;

    // Runtime owns output-path policy and export completion. The Render service
    // remains responsible only for producing an owned CPU image.
    class RuntimeScreenshotService
    {
    public:
        explicit RuntimeScreenshotService(render::IRenderCaptureService &capture_service);

        // A non-empty callback receives exactly one final result. This returns
        // false only for an empty callback; a Render rejection is reported by
        // that callback as CaptureRejected.
        bool RequestScreenshot(ScreenshotRequest request, ScreenshotCallback on_completed);

    private:
        render::IRenderCaptureService &capture_service_;
    };
}

#endif
