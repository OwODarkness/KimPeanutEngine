#ifndef KPENGINE_RUNTIME_LAUNCH_OPTIONS_H
#define KPENGINE_RUNTIME_LAUNCH_OPTIONS_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/type.h"
#include "command/command_local_transport.h"
#include "host/application_host.h"

namespace kpengine::runtime
{
    // Which surface a standalone host's startup capture reads back. The
    // presentation view includes the host's own UI, so it is not a
    // cross-backend comparable image; the product view is the host's own
    // output target, which is.
    enum class StartupCaptureView : uint8_t
    {
        Presentation,
        Product,
    };

    struct RuntimeLaunchOptions
    {
        ApplicationMode application_mode = ApplicationMode::Scene3D;
        GraphicsAPIType graphics_api_type = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        command::LocalCommandTransportConfig command_transport_config{};
        std::optional<std::string> startup_level_override;
        // Optional one-shot capture used by standalone hosts. The Live2D
        // viewer consumes this as a Live2D-target capture; Scene3D keeps its
        // existing command-driven screenshot flow.
        std::optional<std::string> startup_capture_override;
        StartupCaptureView startup_capture_view = StartupCaptureView::Presentation;
        // Whether a standalone host clears its captured target opaquely. A
        // transparent clear is what makes the product's own alpha and blend
        // coverage observable in the exported image.
        bool startup_capture_transparent_clear = false;
        // Whether a standalone host shuts down once its startup capture
        // resolves. Without this the viewer runs until it is killed, so the
        // shutdown path (and its leaked-handle evidence) never executes.
        bool startup_exit_after_capture = false;
    };

    struct RuntimeLaunchOptionsParseResult
    {
        RuntimeLaunchOptions options{};
        std::string diagnostic;
        bool succeeded = false;

        explicit operator bool() const { return succeeded; }
    };

    // Parses command-line options after argv[0]. This overload is convenient for
    // unit tests and keeps the parser independent from Engine construction.
    RuntimeLaunchOptionsParseResult ParseRuntimeLaunchOptions(
        const std::vector<std::string_view> &arguments);

    // Parses a normal C++ entry-point argument array, skipping argv[0].
    RuntimeLaunchOptionsParseResult ParseRuntimeLaunchOptions(int argc, char **argv);
}

#endif // KPENGINE_RUNTIME_LAUNCH_OPTIONS_H
