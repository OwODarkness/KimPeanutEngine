#ifndef KPENGINE_RUNTIME_HOST_STANDALONE_VIEWER_LAUNCH_CONFIGURATION_H
#define KPENGINE_RUNTIME_HOST_STANDALONE_VIEWER_LAUNCH_CONFIGURATION_H

#include <cstdint>
#include <optional>
#include <string>

namespace kpengine::runtime
{
    enum class StartupCaptureView : std::uint8_t
    {
        Presentation,
        Product,
    };

    struct RuntimeResizeRequest
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct StandaloneViewerLaunchConfiguration
    {
        std::optional<std::string> capture_path;
        std::optional<RuntimeResizeRequest> resize;
        StartupCaptureView capture_view = StartupCaptureView::Presentation;
        bool capture_transparent_clear = false;
        bool exit_after_capture = false;
    };
}

#endif // KPENGINE_RUNTIME_HOST_STANDALONE_VIEWER_LAUNCH_CONFIGURATION_H
