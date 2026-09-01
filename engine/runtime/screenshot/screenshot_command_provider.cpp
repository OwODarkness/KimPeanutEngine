#include "screenshot/screenshot_command_provider.h"

#include <string>
#include <utility>

#include "screenshot/runtime_screenshot_service.h"

namespace kpengine::runtime
{
    namespace
    {
        const char *ToString(const ScreenshotResultStatus status)
        {
            switch (status)
            {
            case ScreenshotResultStatus::Exported:
                return "exported";
            case ScreenshotResultStatus::InvalidOutputPath:
                return "invalid_output_path";
            case ScreenshotResultStatus::CaptureRejected:
                return "capture_rejected";
            case ScreenshotResultStatus::CaptureUnavailable:
                return "capture_unavailable";
            case ScreenshotResultStatus::CaptureCancelled:
                return "capture_cancelled";
            case ScreenshotResultStatus::CaptureFailed:
                return "capture_failed";
            case ScreenshotResultStatus::WriteFailed:
                return "write_failed";
            }
            return "unknown";
        }

        command::CommandStatus ToCommandStatus(const ScreenshotResultStatus status)
        {
            if (status == ScreenshotResultStatus::Exported)
            {
                return command::CommandStatus::Success;
            }
            if (status == ScreenshotResultStatus::InvalidOutputPath)
            {
                return command::CommandStatus::InvalidArguments;
            }
            return command::CommandStatus::Failed;
        }

        command::CommandResult MakeResult(ScreenshotResult result, const uint64_t request_id)
        {
            const std::string status = ToString(result.status);
            command::CommandResult command_result{
                ToCommandStatus(result.status),
                result.IsSuccess() ? "Screenshot exported" : result.diagnostic,
                request_id,
                {{"status", status},
                 {"success", result.IsSuccess()},
                 {"output_path", result.output_path},
                 {"diagnostic", result.diagnostic}}};
            return command_result;
        }

        render::CaptureView ToCaptureView(const std::string &view)
        {
            if (view == "linear_depth") return render::CaptureView::LinearDepth;
            if (view == "world_normal") return render::CaptureView::WorldNormal;
            if (view == "base_color") return render::CaptureView::BaseColor;
            if (view == "material_params") return render::CaptureView::MaterialParams;
            if (view == "shadow_visibility") return render::CaptureView::ShadowVisibility;
            if (view == "spot_shadow_depth") return render::CaptureView::SpotShadowDepth;
            if (view == "spot_shadow_visibility") return render::CaptureView::SpotShadowVisibility;
            if (view == "point_shadow_depth") return render::CaptureView::PointShadowDepth;
            if (view == "point_shadow_visibility") return render::CaptureView::PointShadowVisibility;
            return render::CaptureView::SceneColor;
        }
    }

    command::CommandRegistrationResult RegisterScreenshotCommands(
        command::CommandRegistry &registry,
        std::shared_ptr<RuntimeScreenshotService> screenshot_service)
    {
        if (!screenshot_service)
        {
            return {{}, command::CommandRegistrationStatus::InvalidDescriptor,
                    "Screenshot command provider requires a screenshot service"};
        }

        command::CommandDesc descriptor{
            "capture.screenshot",
            "RuntimeScreenshot",
            "Capture a rendered final or diagnostic view and export it as a PNG",
            command::CommandCategory::Render,
            command::CommandFlags::AgentAllowed | command::CommandFlags::LuaAllowed,
            {{command::CommandArgumentDesc{"path", command::CommandValueType::String, false,
                                           {}, {}},
              command::CommandArgumentDesc{"view", command::CommandValueType::Enum, false,
                                           std::string{"scene_color"},
                                           {"scene_color", "linear_depth", "world_normal",
                                            "base_color", "material_params",
                                            "shadow_visibility", "spot_shadow_depth",
                                            "spot_shadow_visibility", "point_shadow_depth",
                                            "point_shadow_visibility"}}}},
            [screenshot_service = std::move(screenshot_service)](
                const command::CommandCall &call, const command::CommandContext &context)
            {
                if (!context.complete)
                {
                    return command::CommandResult{
                        command::CommandStatus::Failed,
                        "Screenshot command requires a deferred Runtime command request",
                        context.request_id,
                        {}};
                }

                ScreenshotRequest request{};
                const auto path = call.arguments.find("path");
                if (path != call.arguments.end())
                {
                    request.output_path = std::get<std::string>(path->second);
                }
                const auto view = call.arguments.find("view");
                if (view != call.arguments.end())
                {
                    request.capture.view =
                        ToCaptureView(std::get<std::string>(view->second));
                }

                const command::CommandCompletionSink complete = context.complete;
                const uint64_t request_id = context.request_id;
                const bool accepted = screenshot_service->RequestScreenshot(
                    std::move(request),
                    [complete, request_id](ScreenshotResult result) mutable
                    {
                        complete(MakeResult(std::move(result), request_id));
                    });
                if (!accepted)
                {
                    return command::CommandResult{
                        command::CommandStatus::Failed,
                        "Screenshot service rejected the request callback",
                        request_id,
                        {}};
                }

                return command::CommandResult{
                    command::CommandStatus::Pending,
                    "Screenshot capture submitted",
                    request_id,
                    {{"status", std::string{"pending"}}, {"success", false}}};
            },
            command::CommandThread::Game};

        return registry.Register(std::move(descriptor));
    }
}
