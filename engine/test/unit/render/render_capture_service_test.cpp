#include <gtest/gtest.h>

#include <optional>
#include <utility>

#include "render/render_capture_service_internal.h"

namespace
{
    using kpengine::render::CaptureRequest;
    using kpengine::render::CaptureResult;
    using kpengine::render::CaptureResultStatus;
    using kpengine::render::CaptureView;
    using kpengine::render::CapturedImage;
    using kpengine::render::RenderCaptureService;

    class FakeReadback final : public kpengine::graphics::IRenderTargetReadback
    {
    public:
        std::optional<kpengine::graphics::RenderTargetReadbackRequest> request;
        kpengine::graphics::RenderTargetReadbackCallback callback;

        bool EnqueueRenderTargetReadback(
            kpengine::graphics::RenderTargetReadbackRequest queued,
            kpengine::graphics::RenderTargetReadbackCallback on_completed) override
        {
            request = queued;
            callback = std::move(on_completed);
            return true;
        }

        void CollectCompletedReadbacks() override {}
        void DrainPendingReadbacks(std::string) override {}
    };
}

TEST(RenderCaptureServiceTest, RejectsUnknownViewsWithoutCreatingPendingWork)
{
    RenderCaptureService service;
    int callback_count = 0;
    CaptureResult result;

    EXPECT_TRUE(service.RequestCapture(
        {static_cast<CaptureView>(255)},
        [&](CaptureResult completed)
        {
            ++callback_count;
            result = std::move(completed);
        }));

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(result.status, CaptureResultStatus::Unavailable);
    EXPECT_FALSE(result.diagnostic.empty());
    EXPECT_FALSE(service.HasPendingCapture());
}

TEST(RenderCaptureServiceTest, AllowsOnlyOnePendingSceneColorRequest)
{
    RenderCaptureService service;
    int first_callback_count = 0;

    EXPECT_TRUE(service.RequestCapture(
        {CaptureView::SceneColor},
        [&](CaptureResult) { ++first_callback_count; }));
    EXPECT_TRUE(service.HasPendingCapture());
    EXPECT_FALSE(service.RequestCapture(
        {CaptureView::SceneColor},
        [](CaptureResult) {}));
    EXPECT_EQ(first_callback_count, 0);
}

TEST(RenderCaptureServiceTest, DefersEngineWindowCaptureUntilAfterPresentation)
{
    RenderCaptureService service;
    CaptureResult result;
    EXPECT_TRUE(service.RequestCapture(
        {CaptureView::EngineWindow},
        [&result](CaptureResult completed) { result = std::move(completed); }));

    EXPECT_TRUE(service.HasPendingCapture());
    EXPECT_TRUE(service.HasPendingWindowCapture());
    EXPECT_FALSE(service.GetPendingView().has_value());
    EXPECT_FALSE(service.EnqueuePendingReadback());

    CapturedImage image{};
    image.width = 1;
    image.height = 1;
    image.rgba8_pixels = {1, 2, 3, 255};
    service.CompletePendingWindowCapture(
        {CaptureResultStatus::Captured, std::move(image), {}});

    EXPECT_TRUE(result.IsSuccess());
    EXPECT_FALSE(service.HasPendingCapture());
}

TEST(RenderCaptureServiceTest, DefersResolvedViewReadbackUntilRenderRecordsItsPass)
{
    FakeReadback readback;
    CaptureView resolved_view = CaptureView::SceneColor;
    RenderCaptureService service{
        &readback,
        [&resolved_view](CaptureView view)
        {
            resolved_view = view;
            return kpengine::graphics::RenderTargetHandle{7, 2};
        },
        [] { return 42U; }};

    EXPECT_TRUE(service.RequestCapture(
        {CaptureView::WorldNormal}, [](CaptureResult) {}));
    EXPECT_EQ(service.GetPendingView(), CaptureView::WorldNormal);
    EXPECT_FALSE(readback.request.has_value());

    EXPECT_TRUE(service.EnqueuePendingReadback());
    EXPECT_EQ(resolved_view, CaptureView::WorldNormal);
    ASSERT_TRUE(readback.request.has_value());
    EXPECT_EQ(readback.request->target,
              (kpengine::graphics::RenderTargetHandle{7, 2}));
    EXPECT_EQ(readback.request->frame_number, 42U);
    EXPECT_FALSE(service.GetPendingView().has_value());
    EXPECT_FALSE(service.EnqueuePendingReadback());
}

TEST(RenderCaptureServiceTest, AcceptsSpotShadowDiagnosticViews)
{
    for (const CaptureView view : {CaptureView::SpotShadowDepth,
                                   CaptureView::SpotShadowVisibility})
    {
        FakeReadback readback;
        CaptureView resolved_view = CaptureView::SceneColor;
        RenderCaptureService service{
            &readback,
            [&resolved_view](CaptureView requested)
            {
                resolved_view = requested;
                return kpengine::graphics::RenderTargetHandle{9, 3};
            },
            [] { return 7U; }};

        EXPECT_TRUE(service.RequestCapture({view}, [](CaptureResult) {}));
        EXPECT_EQ(service.GetPendingView(), view);
        EXPECT_TRUE(service.EnqueuePendingReadback());
        EXPECT_EQ(resolved_view, view);
        ASSERT_TRUE(readback.request.has_value());
        EXPECT_EQ(readback.request->target,
                  (kpengine::graphics::RenderTargetHandle{9, 3}));
    }
}

TEST(RenderCaptureServiceTest, AcceptsPointShadowDiagnosticViews)
{
    for (const CaptureView view : {CaptureView::PointShadowDepth,
                                   CaptureView::PointShadowVisibility})
    {
        FakeReadback readback;
        CaptureView resolved_view = CaptureView::SceneColor;
        RenderCaptureService service{
            &readback,
            [&resolved_view](CaptureView requested)
            {
                resolved_view = requested;
                return kpengine::graphics::RenderTargetHandle{9, 3};
            },
            [] { return 7U; }};
        EXPECT_TRUE(service.RequestCapture({view}, [](CaptureResult) {}));
        EXPECT_TRUE(service.EnqueuePendingReadback());
        EXPECT_EQ(resolved_view, view);
    }
}

TEST(RenderCaptureServiceTest, CompletesPendingCaptureExactlyOnce)
{
    RenderCaptureService service;
    int callback_count = 0;
    CaptureResult result;

    EXPECT_TRUE(service.RequestCapture(
        {CaptureView::SceneColor},
        [&](CaptureResult completed)
        {
            ++callback_count;
            result = std::move(completed);
        }));

    CapturedImage image{};
    image.width = 2;
    image.height = 1;
    image.frame_number = 4;
    image.submission_serial = 7;
    image.rgba8_pixels = {1, 2, 3, 4, 5, 6, 7, 8};
    service.CompletePendingCapture(image);
    service.CompletePendingCapture(image);

    EXPECT_EQ(callback_count, 1);
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.image.width, 2u);
    EXPECT_EQ(result.image.rgba8_pixels, image.rgba8_pixels);
    EXPECT_FALSE(service.HasPendingCapture());
}

TEST(RenderCaptureServiceTest, RejectsMalformedReadbackCompletion)
{
    RenderCaptureService service;
    CaptureResult result;
    EXPECT_TRUE(service.RequestCapture(
        {CaptureView::SceneColor},
        [&result](CaptureResult completed) { result = std::move(completed); }));

    CapturedImage malformed{};
    malformed.width = 1;
    malformed.height = 1;
    malformed.rgba8_pixels = {1, 2, 3};
    service.CompletePendingCapture(std::move(malformed));

    EXPECT_EQ(result.status, CaptureResultStatus::Failed);
    EXPECT_FALSE(result.diagnostic.empty());
}

TEST(RenderCaptureServiceTest, CancelsPendingCaptureOnDestruction)
{
    int callback_count = 0;
    CaptureResult result;
    {
        RenderCaptureService service;
        EXPECT_TRUE(service.RequestCapture(
            {CaptureView::SceneColor},
            [&](CaptureResult completed)
            {
                ++callback_count;
                result = std::move(completed);
            }));
    }

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(result.status, CaptureResultStatus::Cancelled);
    EXPECT_FALSE(result.diagnostic.empty());
}
