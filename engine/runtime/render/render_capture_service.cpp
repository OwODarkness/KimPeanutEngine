#include "render/render_capture_service_internal.h"

#include <utility>

namespace kpengine::render
{
    namespace
    {
        constexpr const char *kUnavailableViewDiagnostic =
            "Requested capture view is not supported";
        constexpr const char *kShutdownDiagnostic =
            "Render capture cancelled during renderer shutdown";

        bool IsSupported(CaptureView view)
        {
            switch (view)
            {
            case CaptureView::SceneColor:
            case CaptureView::LinearDepth:
            case CaptureView::WorldNormal:
            case CaptureView::BaseColor:
            case CaptureView::MaterialParams:
            case CaptureView::ShadowVisibility:
            case CaptureView::SpotShadowDepth:
            case CaptureView::SpotShadowVisibility:
            case CaptureView::PointShadowDepth:
            case CaptureView::PointShadowVisibility:
                return true;
            case CaptureView::EngineWindow:
                return true;
            }
            return false;
        }
    }

    RenderCaptureService::~RenderCaptureService()
    {
        FailPendingCapture(kShutdownDiagnostic);
    }

    RenderCaptureService::RenderCaptureService(
        graphics::IRenderTargetReadback *readback,
        CaptureTargetResolver capture_target,
        std::function<uint64_t()> frame_number)
        : readback_(readback), capture_target_(std::move(capture_target)),
          frame_number_(std::move(frame_number))
    {
    }

    bool RenderCaptureService::RequestCapture(CaptureRequest request,
                                              CapturedImageCallback on_completed)
    {
        if (!on_completed)
        {
            return false;
        }

        if (!IsSupported(request.view))
        {
            on_completed({CaptureResultStatus::Unavailable, {}, kUnavailableViewDiagnostic});
            return true;
        }

        {
            std::scoped_lock lock(mutex_);
            if (pending_capture_.has_value())
            {
                return false;
            }
            pending_capture_ = PendingCapture{request, std::move(on_completed), false};
        }
        return true;
    }

    bool RenderCaptureService::HasPendingCapture() const
    {
        std::scoped_lock lock(mutex_);
        return pending_capture_.has_value();
    }

    std::optional<CaptureView> RenderCaptureService::GetPendingView() const
    {
        std::scoped_lock lock(mutex_);
        if (!pending_capture_.has_value() || pending_capture_->readback_enqueued)
        {
            return std::nullopt;
        }
        if (pending_capture_->request.view == CaptureView::EngineWindow)
        {
            return std::nullopt;
        }
        return pending_capture_->request.view;
    }

    bool RenderCaptureService::HasPendingWindowCapture() const
    {
        std::scoped_lock lock(mutex_);
        return pending_capture_.has_value() &&
               pending_capture_->request.view == CaptureView::EngineWindow;
    }

    bool RenderCaptureService::EnqueuePendingReadback()
    {
        CaptureView view = CaptureView::SceneColor;
        {
            std::scoped_lock lock(mutex_);
            if (!pending_capture_.has_value() || pending_capture_->readback_enqueued)
            {
                return false;
            }
            if (pending_capture_->request.view == CaptureView::EngineWindow)
            {
                return false;
            }
            pending_capture_->readback_enqueued = true;
            view = pending_capture_->request.view;
        }

        if (!capture_target_)
        {
            Complete({CaptureResultStatus::Unavailable, {},
                      "Render capture target resolver is unavailable"});
            return true;
        }
        if (!readback_)
        {
            Complete({CaptureResultStatus::Unavailable, {},
                      "The active graphics backend does not implement render-target readback"});
            return true;
        }

        const graphics::RenderTargetHandle target = capture_target_(view);
        if (!target.IsValid())
        {
            Complete({CaptureResultStatus::Failed, {},
                      "Render capture view did not resolve to a valid color target"});
            return true;
        }
        if (!readback_->EnqueueRenderTargetReadback(
                {target, frame_number_ ? frame_number_() : 0},
                [this](graphics::RenderTargetReadbackResult result)
                {
                    if (result.IsSuccess())
                    {
                        CompletePendingCapture(std::move(result.image));
                        return;
                    }
                    const CaptureResultStatus status =
                        result.status == graphics::RenderTargetReadbackStatus::Cancelled
                            ? CaptureResultStatus::Cancelled
                            : CaptureResultStatus::Failed;
                    Complete({status, {}, std::move(result.diagnostic)});
                }))
        {
            Complete({CaptureResultStatus::Failed, {},
                      "Graphics backend rejected the render-target readback request"});
        }
        return true;
    }

    void RenderCaptureService::CompletePendingCapture(CapturedImage image)
    {
        if (!image.IsValid())
        {
            Complete({CaptureResultStatus::Failed, {},
                      "Readback completion did not contain tightly packed RGBA8 pixels"});
            return;
        }
        Complete({CaptureResultStatus::Captured, std::move(image), {}});
    }

    void RenderCaptureService::CompletePendingWindowCapture(CaptureResult result)
    {
        if (result.status == CaptureResultStatus::Captured && !result.image.IsValid())
        {
            result.status = CaptureResultStatus::Failed;
            result.diagnostic = "Window capture completion did not contain RGBA8 pixels";
            result.image = {};
        }
        Complete(std::move(result));
    }

    void RenderCaptureService::FailPendingCapture(std::string diagnostic)
    {
        Complete({CaptureResultStatus::Cancelled, {}, std::move(diagnostic)});
    }

    void RenderCaptureService::RejectPendingCapture(std::string diagnostic)
    {
        Complete({CaptureResultStatus::Failed, {}, std::move(diagnostic)});
    }

    void RenderCaptureService::Complete(CaptureResult result)
    {
        CapturedImageCallback callback;
        {
            std::scoped_lock lock(mutex_);
            if (!pending_capture_.has_value())
            {
                return;
            }
            callback = std::move(pending_capture_->on_completed);
            pending_capture_.reset();
        }
        callback(std::move(result));
    }
}
