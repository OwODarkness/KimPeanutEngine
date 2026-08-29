#include <gtest/gtest.h>

#include "render/render_capture_service_internal.h"

namespace
{
    using kpengine::render::CaptureRequest;
    using kpengine::render::CaptureResult;
    using kpengine::render::CaptureResultStatus;
    using kpengine::render::CaptureView;
    using kpengine::render::CapturedImage;
    using kpengine::render::RenderCaptureService;
}

TEST(RenderCaptureServiceTest, ReservesUnavailableViewsWithoutCreatingPendingWork)
{
    RenderCaptureService service;
    int callback_count = 0;
    CaptureResult result;

    EXPECT_TRUE(service.RequestCapture(
        {CaptureView::WorldNormal},
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
