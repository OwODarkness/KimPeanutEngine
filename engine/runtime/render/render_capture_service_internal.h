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
        using CaptureTargetResolver =
            std::function<graphics::RenderTargetHandle(CaptureView)>;

        RenderCaptureService(graphics::IRenderTargetReadback *readback = nullptr,
                             CaptureTargetResolver capture_target = {},
                             std::function<uint64_t()> frame_number = {});
        ~RenderCaptureService() override;

        bool RequestCapture(CaptureRequest request,
                            CapturedImageCallback on_completed) override;

        bool HasPendingCapture() const;
        std::optional<CaptureView> GetPendingView() const;
        bool EnqueuePendingReadback();
        void CompletePendingCapture(CapturedImage image);
        void FailPendingCapture(std::string diagnostic);
        void RejectPendingCapture(std::string diagnostic);

    private:
        struct PendingCapture
        {
            CaptureRequest request;
            CapturedImageCallback on_completed;
            bool readback_enqueued = false;
        };

        void Complete(CaptureResult result);

        mutable std::mutex mutex_;
        std::optional<PendingCapture> pending_capture_;
        graphics::IRenderTargetReadback *readback_ = nullptr;
        CaptureTargetResolver capture_target_;
        std::function<uint64_t()> frame_number_;
    };
}

#endif
