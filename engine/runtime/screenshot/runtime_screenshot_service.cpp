#include "screenshot/runtime_screenshot_service.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include "image_io/image_io.h"

namespace kpengine::runtime
{
    namespace
    {
        constexpr char k_capture_root[] = "save/screenshots";
        constexpr char k_validation_directory[] = "save/screenshots/validation";

        struct ExportRequest
        {
            std::optional<std::filesystem::path> explicit_path;
        };

        bool IsInside(const std::filesystem::path &path, const std::filesystem::path &root)
        {
            auto path_part = path.begin();
            for (const auto &root_part : root)
            {
                if (path_part == path.end() || *path_part != root_part)
                {
                    return false;
                }
                ++path_part;
            }
            return path_part != path.end();
        }

        std::optional<std::filesystem::path> ValidateExplicitPath(const std::string &output_path,
                                                                   std::string &diagnostic)
        {
            const std::filesystem::path raw_path{output_path};
            if (raw_path.empty() || raw_path.is_absolute())
            {
                diagnostic = "Screenshot output path must be a relative validation path";
                return std::nullopt;
            }

            for (const auto &part : raw_path)
            {
                if (part == "..")
                {
                    diagnostic = "Screenshot output path must not contain traversal";
                    return std::nullopt;
                }
            }

            const std::filesystem::path normalized_path = raw_path.lexically_normal();
            const std::filesystem::path validation_root{k_validation_directory};
            if (!IsInside(normalized_path, validation_root) || normalized_path.extension() != ".png")
            {
                diagnostic = "Screenshot output path must be a PNG below save/screenshots/validation";
                return std::nullopt;
            }
            return normalized_path;
        }

        std::filesystem::path MakeDefaultPath(uint64_t frame_number)
        {
            const auto now = std::chrono::system_clock::now();
            const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm utc_time{};
            gmtime_s(&utc_time, &time);

            std::ostringstream name;
            name << std::put_time(&utc_time, "%Y%m%d-%H%M%S-")
                 << std::setw(3) << std::setfill('0') << milliseconds
                 << "-f" << frame_number << ".png";
            return std::filesystem::path{k_capture_root} / name.str();
        }

        std::optional<std::filesystem::path> FindAvailablePath(std::filesystem::path path,
                                                                std::string &diagnostic)
        {
            std::error_code error;
            if (!std::filesystem::exists(path, error))
            {
                if (error)
                {
                    diagnostic = "Could not inspect screenshot output path: " + error.message();
                    return std::nullopt;
                }
                return path;
            }

            const std::filesystem::path directory = path.parent_path();
            const std::string stem = path.stem().string();
            const std::string extension = path.extension().string();
            for (uint32_t sequence = 1; sequence != 0; ++sequence)
            {
                path = directory / (stem + "-" + std::to_string(sequence) + extension);
                if (!std::filesystem::exists(path, error))
                {
                    if (error)
                    {
                        diagnostic = "Could not inspect screenshot output path: " + error.message();
                        return std::nullopt;
                    }
                    return path;
                }
            }

            diagnostic = "No available screenshot filename could be generated";
            return std::nullopt;
        }

        ScreenshotResult MakeCaptureFailure(const render::CaptureResult &capture_result)
        {
            ScreenshotResult result{};
            result.diagnostic = capture_result.diagnostic;
            switch (capture_result.status)
            {
            case render::CaptureResultStatus::Unavailable:
                result.status = ScreenshotResultStatus::CaptureUnavailable;
                break;
            case render::CaptureResultStatus::Cancelled:
                result.status = ScreenshotResultStatus::CaptureCancelled;
                break;
            default:
                result.status = ScreenshotResultStatus::CaptureFailed;
                break;
            }
            return result;
        }

        void ExportCapture(const ExportRequest &request, ScreenshotCallback &on_completed,
                           render::CaptureResult capture_result)
        {
            if (!capture_result.IsSuccess())
            {
                on_completed(MakeCaptureFailure(capture_result));
                return;
            }

            std::filesystem::path requested_path = request.explicit_path.value_or(
                MakeDefaultPath(capture_result.image.frame_number));
            std::string diagnostic;
            const auto output_path = FindAvailablePath(std::move(requested_path), diagnostic);
            if (!output_path.has_value())
            {
                on_completed({ScreenshotResultStatus::WriteFailed, {}, std::move(diagnostic)});
                return;
            }

            std::error_code error;
            std::filesystem::create_directories(output_path->parent_path(), error);
            if (error)
            {
                on_completed({ScreenshotResultStatus::WriteFailed, output_path->string(),
                              "Could not create screenshot directory: " + error.message()});
                return;
            }

            image_io::ImageBuffer image{};
            image.width = capture_result.image.width;
            image.height = capture_result.image.height;
            image.format = image_io::ImagePixelFormat::Rgba8;
            image.pixels = std::move(capture_result.image.rgba8_pixels);
            image_io::ImageIoResult write_result = image_io::WritePng(image, output_path->string());
            if (!write_result.success)
            {
                on_completed({ScreenshotResultStatus::WriteFailed, output_path->string(),
                              std::move(write_result.diagnostic)});
                return;
            }

            on_completed({ScreenshotResultStatus::Exported, output_path->string(), {}});
        }
    }

    RuntimeScreenshotService::RuntimeScreenshotService(render::IRenderCaptureService &capture_service)
        : capture_service_(capture_service)
    {
    }

    bool RuntimeScreenshotService::RequestScreenshot(ScreenshotRequest request,
                                                      ScreenshotCallback on_completed)
    {
        if (!on_completed)
        {
            return false;
        }

        auto export_request = std::make_shared<ExportRequest>();
        if (!request.output_path.empty())
        {
            std::string diagnostic;
            export_request->explicit_path = ValidateExplicitPath(request.output_path, diagnostic);
            if (!export_request->explicit_path.has_value())
            {
                on_completed({ScreenshotResultStatus::InvalidOutputPath, {}, std::move(diagnostic)});
                return true;
            }
        }

        auto callback = std::make_shared<ScreenshotCallback>(std::move(on_completed));
        const bool accepted = capture_service_.RequestCapture(
            request.capture,
            [export_request, callback](render::CaptureResult capture_result)
            {
                ExportCapture(*export_request, *callback, std::move(capture_result));
            });
        if (!accepted)
        {
            (*callback)({ScreenshotResultStatus::CaptureRejected, {},
                         "Render capture service rejected the screenshot request"});
        }
        return true;
    }
}
