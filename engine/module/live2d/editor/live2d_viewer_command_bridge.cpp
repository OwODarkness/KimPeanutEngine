#include "live2d_viewer_command_bridge.h"

#include <utility>

#include "screenshot/screenshot_command_provider.h"

namespace kpengine::live2d::editor
{
    bool Live2DViewerCommandBridge::Register(
        runtime::command::CommandRegistry &registry,
        Live2DViewerCommandSources sources,
        std::string &diagnostic)
    {
        diagnostic.clear();
        if (!sources.model_report || !sources.screenshot_service)
        {
            diagnostic = "Live2D viewer command sources are incomplete";
            return false;
        }

        runtime::command::CommandRegistrationResult model_report =
            RegisterLive2DModelReportCommand(registry, std::move(sources.model_report));
        if (!model_report.IsSuccess())
        {
            diagnostic = model_report.diagnostic;
            return false;
        }

        runtime::command::CommandRegistrationResult screenshot =
            runtime::RegisterScreenshotCommands(registry,
                                                std::move(sources.screenshot_service));
        if (!screenshot.IsSuccess())
        {
            diagnostic = screenshot.diagnostic;
            return false;
        }

        model_report_registration_ = std::move(model_report.registration);
        screenshot_registration_ = std::move(screenshot.registration);
        return true;
    }

    void Live2DViewerCommandBridge::Reset() noexcept
    {
        screenshot_registration_ = {};
        model_report_registration_ = {};
    }
}
