#ifndef KPENGINE_LIVE2D_VIEWER_COMMAND_BRIDGE_H
#define KPENGINE_LIVE2D_VIEWER_COMMAND_BRIDGE_H

#include <functional>
#include <string>

#include "command/command_registry.h"
#include "module/live2d/live2d_model_report_command.h"

namespace kpengine::runtime
{
    class RuntimeScreenshotService;
}

namespace kpengine::live2d::editor
{
    struct Live2DViewerCommandSources final
    {
        Live2DModelReportResolver model_report;
        std::function<runtime::RuntimeScreenshotService *()> screenshot_service;
    };

    class Live2DViewerCommandBridge final
    {
    public:
        bool Register(runtime::command::CommandRegistry &registry,
                      Live2DViewerCommandSources sources,
                      std::string &diagnostic);
        void Reset() noexcept;

    private:
        runtime::command::CommandRegistration model_report_registration_;
        runtime::command::CommandRegistration screenshot_registration_;
    };
}

#endif // KPENGINE_LIVE2D_VIEWER_COMMAND_BRIDGE_H
