#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

#include "screenshot/runtime_screenshot_service.h"

namespace kpengine::runtime
{
    namespace
    {
        class FakeCaptureService final : public render::IRenderCaptureService
        {
        public:
            bool accepted = true;
            render::CaptureResult result{render::CaptureResultStatus::Captured,
                                         {1, 1, 42, 7, {10, 20, 30, 255}}, {}};

            bool RequestCapture(render::CaptureRequest, render::CapturedImageCallback on_completed) override
            {
                if (!accepted)
                {
                    return false;
                }
                on_completed(result);
                return true;
            }
        };

        ScreenshotResult Request(RuntimeScreenshotService &service, std::string output_path = {})
        {
            std::optional<ScreenshotResult> result;
            EXPECT_TRUE(service.RequestScreenshot({{}, std::move(output_path)},
                                                  [&result](ScreenshotResult completed)
                                                  { result = std::move(completed); }));
            EXPECT_TRUE(result.has_value());
            return std::move(*result);
        }

        void RemoveFile(const std::string &path)
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }

    TEST(RuntimeScreenshotServiceTest, ExportsDefaultUtcNamedPng)
    {
        FakeCaptureService capture_service;
        RuntimeScreenshotService service{capture_service};

        const ScreenshotResult result = Request(service);
        EXPECT_TRUE(result.IsSuccess());
        const std::string normalized_path = std::filesystem::path{result.output_path}.generic_string();
        EXPECT_EQ(normalized_path.rfind("save/screenshots/", 0), 0U);
        EXPECT_NE(normalized_path.find("-f42.png"), std::string::npos);
        EXPECT_TRUE(std::filesystem::exists(result.output_path));
        RemoveFile(result.output_path);
    }

    TEST(RuntimeScreenshotServiceTest, RejectsPathsOutsideValidationDirectory)
    {
        FakeCaptureService capture_service;
        RuntimeScreenshotService service{capture_service};

        const ScreenshotResult traversal = Request(service, "save/screenshots/validation/../outside.png");
        EXPECT_EQ(traversal.status, ScreenshotResultStatus::InvalidOutputPath);

        const ScreenshotResult outside = Request(service, "save/screenshots/not-validation.png");
        EXPECT_EQ(outside.status, ScreenshotResultStatus::InvalidOutputPath);

        const ScreenshotResult absolute = Request(service,
                                                  std::filesystem::absolute("outside.png").string());
        EXPECT_EQ(absolute.status, ScreenshotResultStatus::InvalidOutputPath);
    }

    TEST(RuntimeScreenshotServiceTest, SuffixesAnExistingValidationCapture)
    {
        const std::string requested_path = "save/screenshots/validation/runtime-screenshot-collision.png";
        std::filesystem::create_directories(std::filesystem::path{requested_path}.parent_path());
        {
            std::ofstream existing{requested_path, std::ios::binary};
            existing << "existing";
        }

        FakeCaptureService capture_service;
        RuntimeScreenshotService service{capture_service};
        const ScreenshotResult result = Request(service, requested_path);

        EXPECT_TRUE(result.IsSuccess());
        EXPECT_EQ(std::filesystem::path{result.output_path}.generic_string(),
                  "save/screenshots/validation/runtime-screenshot-collision-1.png");
        EXPECT_TRUE(std::filesystem::exists(result.output_path));
        RemoveFile(requested_path);
        RemoveFile(result.output_path);
    }

    TEST(RuntimeScreenshotServiceTest, ReportsImageWriteFailure)
    {
        FakeCaptureService capture_service;
        capture_service.result.image.width = 0;
        RuntimeScreenshotService service{capture_service};

        const ScreenshotResult result = Request(service, "save/screenshots/validation/runtime-screenshot-invalid.png");
        EXPECT_EQ(result.status, ScreenshotResultStatus::WriteFailed);
        EXPECT_FALSE(result.diagnostic.empty());
    }
}
