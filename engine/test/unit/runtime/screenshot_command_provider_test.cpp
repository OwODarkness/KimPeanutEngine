#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "command/command_registry.h"
#include "screenshot/runtime_screenshot_service.h"
#include "screenshot/screenshot_command_provider.h"

namespace kpengine::runtime
{
    namespace
    {
        class FakeCaptureService final : public render::IRenderCaptureService
        {
        public:
            render::CapturedImageCallback pending_callback;
            render::CaptureRequest last_request;

            bool RequestCapture(render::CaptureRequest request,
                                render::CapturedImageCallback on_completed) override
            {
                last_request = request;
                pending_callback = std::move(on_completed);
                return true;
            }

            void Complete()
            {
                ASSERT_TRUE(pending_callback);
                pending_callback({render::CaptureResultStatus::Captured,
                                  {2, 1, 91, 4, {10, 20, 30, 255, 40, 50, 60, 255}}, {}});
                pending_callback = {};
            }
        };

        const std::string *GetString(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr : std::get_if<std::string>(&iterator->second);
        }

        void RemoveFile(const std::string &path)
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }

    TEST(ScreenshotCommandProviderTest, CompletesQueuedCaptureWithStructuredExportResult)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());

        const std::string output_path =
            "save/screenshots/validation/command-provider-test.png";
        RemoveFile(output_path);
        std::optional<command::CommandResult> callback_result;
        const command::CommandResult pending = registry.Execute(
            {"capture.screenshot", {{"path", std::string{output_path}},
                                    {"view", std::string{"scene_color"}}}},
            {command::CommandOrigin::Agent, command::CommandThread::Immediate},
            [&callback_result](const command::CommandResult &result)
            { callback_result = result; });

        ASSERT_EQ(pending.status, command::CommandStatus::Pending) << pending.message;
        ASSERT_NE(pending.request_id, 0U);
        EXPECT_EQ(registry.PumpGameThread(), 1U);
        EXPECT_FALSE(callback_result.has_value());

        capture_service.Complete();

        ASSERT_TRUE(callback_result.has_value());
        EXPECT_EQ(callback_result->status, command::CommandStatus::Success);
        EXPECT_EQ(callback_result->request_id, pending.request_id);
        ASSERT_NE(GetString(callback_result->data, "status"), nullptr);
        EXPECT_EQ(*GetString(callback_result->data, "status"), "exported");
        ASSERT_NE(GetString(callback_result->data, "output_path"), nullptr);
        EXPECT_EQ(std::filesystem::path{*GetString(callback_result->data, "output_path")}
                      .generic_string(),
                  std::filesystem::path{output_path}.generic_string());
        EXPECT_TRUE(std::filesystem::exists(output_path));

        const auto completion = registry.TakeCompletion(pending.request_id);
        ASSERT_TRUE(completion.has_value());
        EXPECT_EQ(completion->status, command::CommandStatus::Success);
        RemoveFile(output_path);
    }

    TEST(ScreenshotCommandProviderTest, MapsDiagnosticViewNamesToRenderSemantics)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());

        const command::CommandResult pending = registry.Execute(
            {"capture.screenshot", {{"view", std::string{"shadow_visibility"}}}},
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [](const command::CommandResult &) {});
        ASSERT_EQ(pending.status, command::CommandStatus::Pending);
        EXPECT_EQ(registry.PumpGameThread(), 1U);
        EXPECT_EQ(capture_service.last_request.view,
                  render::CaptureView::ShadowVisibility);
    }

    TEST(ScreenshotCommandProviderTest, MapsSpotShadowDiagnosticViewNames)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());
        const auto pending = registry.Execute(
            {"capture.screenshot", {{"view", std::string{"spot_shadow_visibility"}}}},
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [](const command::CommandResult &) {});
        ASSERT_EQ(pending.status, command::CommandStatus::Pending);
        EXPECT_EQ(registry.PumpGameThread(), 1U);
        EXPECT_EQ(capture_service.last_request.view, render::CaptureView::SpotShadowVisibility);
    }

    TEST(ScreenshotCommandProviderTest, MapsSpotShadowDepthDiagnosticViewName)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());
        const auto pending = registry.Execute(
            {"capture.screenshot", {{"view", std::string{"spot_shadow_depth"}}}},
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [](const command::CommandResult &) {});
        ASSERT_EQ(registry.PumpGameThread(), 1U);
        ASSERT_EQ(pending.status, command::CommandStatus::Pending);
        EXPECT_EQ(capture_service.last_request.view, render::CaptureView::SpotShadowDepth);
    }

    TEST(ScreenshotCommandProviderTest, MapsPointShadowDiagnosticViewNames)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());
        const auto pending = registry.Execute(
            {"capture.screenshot", {{"view", std::string{"point_shadow_visibility"}}}},
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [](const command::CommandResult &) {});
        ASSERT_EQ(registry.PumpGameThread(), 1U);
        ASSERT_EQ(pending.status, command::CommandStatus::Pending);
        EXPECT_EQ(capture_service.last_request.view, render::CaptureView::PointShadowVisibility);
    }

    TEST(ScreenshotCommandProviderTest, MapsEngineWindowViewName)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        const command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());

        const auto pending = registry.Execute(
            {"capture.screenshot", {{"view", std::string{"engine_window"}}}},
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [](const command::CommandResult &) {});
        ASSERT_EQ(pending.status, command::CommandStatus::Pending) << pending.message;
        ASSERT_EQ(registry.PumpGameThread(), 1U);
        EXPECT_EQ(capture_service.last_request.view, render::CaptureView::EngineWindow);
    }

    TEST(ScreenshotCommandProviderTest, PreservesServiceOwnedInvalidPathDiagnostic)
    {
        FakeCaptureService capture_service;
        auto screenshot_service = std::make_shared<RuntimeScreenshotService>(capture_service);
        command::CommandRegistry registry;
        command::CommandRegistrationResult registration =
            RegisterScreenshotCommands(registry, screenshot_service);
        ASSERT_TRUE(registration.IsSuccess());

        std::optional<command::CommandResult> callback_result;
        const command::CommandResult pending = registry.Execute(
            {"capture.screenshot",
             {{"path", std::string{"save/screenshots/not-validation.png"}}}},
            {command::CommandOrigin::Test, command::CommandThread::Immediate},
            [&callback_result](const command::CommandResult &result)
            { callback_result = result; });
        ASSERT_EQ(pending.status, command::CommandStatus::Pending) << pending.message;

        EXPECT_EQ(registry.PumpGameThread(), 1U);
        ASSERT_TRUE(callback_result.has_value());
        EXPECT_EQ(callback_result->status, command::CommandStatus::InvalidArguments);
        ASSERT_NE(GetString(callback_result->data, "status"), nullptr);
        EXPECT_EQ(*GetString(callback_result->data, "status"), "invalid_output_path");
        ASSERT_NE(GetString(callback_result->data, "diagnostic"), nullptr);
        EXPECT_FALSE(GetString(callback_result->data, "diagnostic")->empty());
        EXPECT_FALSE(capture_service.pending_callback);
    }
}
