#ifndef KPENGINE_RUNTIME_LAUNCH_OPTIONS_H
#define KPENGINE_RUNTIME_LAUNCH_OPTIONS_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/type.h"
#include "command/command_local_transport.h"

namespace kpengine::runtime
{
    struct RuntimeLaunchOptions
    {
        GraphicsAPIType graphics_api_type = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        command::LocalCommandTransportConfig command_transport_config{};
        std::optional<std::string> startup_level_override;
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
