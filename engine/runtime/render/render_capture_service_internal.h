#ifndef KPENGINE_RUNTIME_RENDER_CAPTURE_SERVICE_INTERNAL_H
#define KPENGINE_RUNTIME_RENDER_CAPTURE_SERVICE_INTERNAL_H

#include <mutex>
#include <optional>
#include <string>
#include <functional>

#include "render/render_capture_service.h"

namespace kpengine::render
{
    // RenderSystem owns this implementation and supplies its completion only
    // after the future Graphics readback seam has made pixels safe to consume.
    class RenderCaptureService final : public IRenderCaptureService
    {
    public:
        RenderCaptureService(graphics::IRenderTargetReadback *readback = nullptr,
                             std::function<graphics::RenderTargetHandle()> scene_color_target = {},
                             std::function<uint64_t()> frame_number = {});
        ~RenderCaptureService() override;

        bool RequestCapture(CaptureRequest request,
                            CapturedImageCallback on_completed) override;

        bool HasPendingCapture() const;
        void CompletePendingCapture(CapturedImage image);
        void FailPendingCapture(std::string diagnostic);

    private:
        struct PendingCapture
        {
            CaptureRequest request;
            CapturedImageCallback on_completed;
        };

        void Complete(CaptureResult result);

        mutable std::mutex mutex_;
        std::optional<PendingCapture> pending_capture_;
        graphics::IRenderTargetReadback *readback_ = nullptr;
        std::function<graphics::RenderTargetHandle()> scene_color_target_;
        std::function<uint64_t()> frame_number_;
    };
}

#endif
