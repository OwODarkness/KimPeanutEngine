#include "render/render_capture_service_internal.h"

#include <utility>

namespace kpengine::render
{
    namespace
    {
        constexpr const char *kUnavailableViewDiagnostic =
            "Requested capture view is not implemented";
        constexpr const char *kShutdownDiagnostic =
            "Render capture cancelled during renderer shutdown";
    }

    RenderCaptureService::~RenderCaptureService()
    {
        FailPendingCapture(kShutdownDiagnostic);
    }

    RenderCaptureService::RenderCaptureService(
        graphics::IRenderTargetReadback *readback,
        std::function<graphics::RenderTargetHandle()> scene_color_target,
        std::function<uint64_t()> frame_number)
        : readback_(readback), scene_color_target_(std::move(scene_color_target)),
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

        if (request.view != CaptureView::SceneColor)
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
            pending_capture_ = PendingCapture{request, std::move(on_completed)};
        }
        if (scene_color_target_ && readback_)
        {
            const graphics::RenderTargetHandle target = scene_color_target_();
            if (!readback_->EnqueueRenderTargetReadback(
                    {target, frame_number_ ? frame_number_() : 0}, [this](graphics::RenderTargetReadbackResult result)
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
                          "Graphics backend rejected the SceneColor readback request"});
            }
        }
        else if (scene_color_target_)
        {
            Complete({CaptureResultStatus::Unavailable, {},
                      "The active graphics backend does not implement render-target readback"});
        }
        return true;
    }

    bool RenderCaptureService::HasPendingCapture() const
    {
        std::scoped_lock lock(mutex_);
        return pending_capture_.has_value();
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

    void RenderCaptureService::FailPendingCapture(std::string diagnostic)
    {
        Complete({CaptureResultStatus::Cancelled, {}, std::move(diagnostic)});
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
