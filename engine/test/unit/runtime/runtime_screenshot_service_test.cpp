#include <filesystem>
#include <array>
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
            uint32_t request_count = 0;
            render::CaptureResult result{render::CaptureResultStatus::Captured,
                                         {1, 1, 42, 7, {10, 20, 30, 255}}, {}};

            bool RequestCapture(render::CaptureRequest, render::CapturedImageCallback on_completed) override
            {
                ++request_count;
                if (!accepted)
                {
                    return false;
                }
                on_completed(result);
                return true;
            }
        };

        ScreenshotResult Request(RuntimeScreenshotService &service, std::string output_path = {},
                                 uint32_t max_dimension = 0)
        {
            std::optional<ScreenshotResult> result;
            EXPECT_TRUE(service.RequestScreenshot({{}, std::move(output_path), max_dimension},
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

    TEST(RuntimeScreenshotServiceTest, DownsamplesOnlyTheExportAndPreservesAspectRatio)
    {
        FakeCaptureService capture_service;
        capture_service.result.image = {
            4, 2, 42, 7,
            {0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255,
             0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255}};
        RuntimeScreenshotService service{capture_service};
        const std::string path = "save/screenshots/validation/runtime-screenshot-preview.png";
        RemoveFile(path);

        const ScreenshotResult result = Request(service, path, 2);
        ASSERT_TRUE(result.IsSuccess()) << result.diagnostic;

        std::array<uint8_t, 24> header{};
        std::ifstream input{result.output_path, std::ios::binary};
        input.read(reinterpret_cast<char *>(header.data()),
                   static_cast<std::streamsize>(header.size()));
        ASSERT_EQ(input.gcount(), static_cast<std::streamsize>(header.size()));
        const auto ReadBigEndian = [&header](size_t offset)
        {
            return (static_cast<uint32_t>(header[offset]) << 24) |
                   (static_cast<uint32_t>(header[offset + 1]) << 16) |
                   (static_cast<uint32_t>(header[offset + 2]) << 8) |
                   static_cast<uint32_t>(header[offset + 3]);
        };
        EXPECT_EQ(ReadBigEndian(16), 2u);
        EXPECT_EQ(ReadBigEndian(20), 1u);
        RemoveFile(path);
    }

    TEST(RuntimeScreenshotServiceTest, RejectsUnboundedPreviewDimensionsBeforeCapture)
    {
        FakeCaptureService capture_service;
        RuntimeScreenshotService service{capture_service};
        std::optional<ScreenshotResult> result;
        EXPECT_TRUE(service.RequestScreenshot({{}, {}, 8193},
                                              [&result](ScreenshotResult completed)
                                              { result = std::move(completed); }));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->status, ScreenshotResultStatus::InvalidDimensions);
        EXPECT_EQ(capture_service.request_count, 0u);
    }

    TEST(RuntimeScreenshotServiceTest, ResolvesCurrentCaptureServiceForEachRequest)
    {
        FakeCaptureService first_service;
        FakeCaptureService replacement_service;
        render::IRenderCaptureService *current_service = &first_service;
        RuntimeScreenshotService service{[&current_service] { return current_service; }};

        const std::string first_path =
            "save/screenshots/validation/runtime-screenshot-before-replacement.png";
        const std::string replacement_path =
            "save/screenshots/validation/runtime-screenshot-after-replacement.png";
        RemoveFile(first_path);
        RemoveFile(replacement_path);
        EXPECT_TRUE(Request(service, first_path).IsSuccess());

        current_service = &replacement_service;
        EXPECT_TRUE(Request(service, replacement_path).IsSuccess());
        EXPECT_EQ(first_service.request_count, 1u);
        EXPECT_EQ(replacement_service.request_count, 1u);
        RemoveFile(first_path);
        RemoveFile(replacement_path);
    }
}
