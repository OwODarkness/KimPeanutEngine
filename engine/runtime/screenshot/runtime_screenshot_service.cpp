#include "screenshot/runtime_screenshot_service.h"

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

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
            uint32_t max_dimension = 0;
        };

        float SrgbToLinear(uint8_t value)
        {
            static const std::array<float, 256> lookup = []
            {
                std::array<float, 256> values{};
                for (size_t index = 0; index < values.size(); ++index)
                {
                    const float encoded = static_cast<float>(index) / 255.0f;
                    values[index] = encoded <= 0.04045f
                                        ? encoded / 12.92f
                                        : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
                }
                return values;
            }();
            return lookup[value];
        }

        uint8_t LinearToSrgb(float value)
        {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            const float encoded = clamped <= 0.0031308f
                                      ? clamped * 12.92f
                                      : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(std::lround(encoded * 255.0f));
        }

        void Downsample(render::CapturedImage &image, uint32_t max_dimension)
        {
            const uint32_t source_max = std::max(image.width, image.height);
            if (max_dimension == 0 || max_dimension >= source_max)
            {
                return;
            }

            const double scale = static_cast<double>(max_dimension) / source_max;
            const uint32_t target_width = std::max(
                1u, static_cast<uint32_t>(std::lround(image.width * scale)));
            const uint32_t target_height = std::max(
                1u, static_cast<uint32_t>(std::lround(image.height * scale)));
            std::vector<uint8_t> target(static_cast<size_t>(target_width) * target_height * 4);
            for (uint32_t y = 0; y < target_height; ++y)
            {
                const double y0 = static_cast<double>(y) * image.height / target_height;
                const double y1 = static_cast<double>(y + 1) * image.height / target_height;
                for (uint32_t x = 0; x < target_width; ++x)
                {
                    const double x0 = static_cast<double>(x) * image.width / target_width;
                    const double x1 = static_cast<double>(x + 1) * image.width / target_width;
                    std::array<double, 4> sum{};
                    double total_weight = 0.0;
                    for (uint32_t source_y = static_cast<uint32_t>(y0);
                         source_y < static_cast<uint32_t>(std::ceil(y1)); ++source_y)
                    {
                        const double wy = std::max(
                            0.0, std::min(y1, static_cast<double>(source_y + 1)) -
                                     std::max(y0, static_cast<double>(source_y)));
                        for (uint32_t source_x = static_cast<uint32_t>(x0);
                             source_x < static_cast<uint32_t>(std::ceil(x1)); ++source_x)
                        {
                            const double wx = std::max(
                                0.0, std::min(x1, static_cast<double>(source_x + 1)) -
                                         std::max(x0, static_cast<double>(source_x)));
                            const double weight = wx * wy;
                            const size_t source_offset =
                                (static_cast<size_t>(source_y) * image.width + source_x) * 4;
                            sum[0] += SrgbToLinear(image.rgba8_pixels[source_offset]) * weight;
                            sum[1] += SrgbToLinear(image.rgba8_pixels[source_offset + 1]) * weight;
                            sum[2] += SrgbToLinear(image.rgba8_pixels[source_offset + 2]) * weight;
                            sum[3] += image.rgba8_pixels[source_offset + 3] / 255.0 * weight;
                            total_weight += weight;
                        }
                    }
                    const size_t target_offset =
                        (static_cast<size_t>(y) * target_width + x) * 4;
                    target[target_offset] = LinearToSrgb(static_cast<float>(sum[0] / total_weight));
                    target[target_offset + 1] = LinearToSrgb(static_cast<float>(sum[1] / total_weight));
                    target[target_offset + 2] = LinearToSrgb(static_cast<float>(sum[2] / total_weight));
                    target[target_offset + 3] = static_cast<uint8_t>(std::lround(
                        std::clamp(sum[3] / total_weight, 0.0, 1.0) * 255.0));
                }
            }
            image.width = target_width;
            image.height = target_height;
            image.rgba8_pixels = std::move(target);
        }

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

            Downsample(capture_result.image, request.max_dimension);

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

        if (request.max_dimension > 8192)
        {
            on_completed({ScreenshotResultStatus::InvalidDimensions, {},
                          "Screenshot max dimension must be between 1 and 8192"});
            return true;
        }

        auto export_request = std::make_shared<ExportRequest>();
        export_request->max_dimension = request.max_dimension;
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
