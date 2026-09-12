#include "launch_options.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <utility>
#include <vector>

#include "asset/utility.h"

namespace kpengine::runtime
{
    namespace
    {
        RuntimeLaunchOptionsParseResult Failure(std::string diagnostic)
        {
            RuntimeLaunchOptionsParseResult result{};
            result.diagnostic = std::move(diagnostic);
            return result;
        }

        bool HasMissingValue(const std::vector<std::string_view> &arguments,
                             const std::size_t option_index)
        {
            return option_index + 1 >= arguments.size() ||
                   arguments[option_index + 1].rfind("--", 0) == 0;
        }

        bool ParsePort(const std::string_view value, uint16_t &port)
        {
            if (value.empty())
            {
                return false;
            }

            unsigned int parsed = 0;
            const auto parsed_result = std::from_chars(
                value.data(), value.data() + value.size(), parsed, 10);
            if (parsed_result.ec != std::errc{} ||
                parsed_result.ptr != value.data() + value.size() || parsed == 0 ||
                parsed > 65535)
            {
                return false;
            }
            port = static_cast<uint16_t>(parsed);
            return true;
        }

        // Extents are written as WIDTHxHEIGHT in validation notes. Both
        // components must be non-zero and bounded so a typo cannot ask a host
        // to allocate an unbounded render target.
        constexpr uint32_t kMaximumResizeExtent = 16384u;

        bool ParseExtentComponent(const std::string_view value, uint32_t &component)
        {
            if (value.empty())
            {
                return false;
            }

            unsigned int parsed = 0;
            const auto parsed_result = std::from_chars(
                value.data(), value.data() + value.size(), parsed, 10);
            if (parsed_result.ec != std::errc{} ||
                parsed_result.ptr != value.data() + value.size())
            {
                return false;
            }
            component = static_cast<uint32_t>(parsed);
            return true;
        }

        bool ParseResizeExtent(const std::string_view value, RuntimeResizeRequest &resize)
        {
            const std::size_t separator = value.find('x');
            if (separator == std::string_view::npos || separator == 0u ||
                separator + 1u >= value.size())
            {
                return false;
            }

            uint32_t width = 0;
            uint32_t height = 0;
            if (!ParseExtentComponent(value.substr(0u, separator), width) ||
                !ParseExtentComponent(value.substr(separator + 1u), height))
            {
                return false;
            }
            if (width == 0u || height == 0u || width > kMaximumResizeExtent ||
                height > kMaximumResizeExtent)
            {
                return false;
            }

            resize.width = width;
            resize.height = height;
            return true;
        }

        bool ParseStartupLevel(std::string_view value, std::string &normalized)
        {
            const std::string authored_path{value};
            if (!asset::NormalizeAssetRootRelativePath(
                    authored_path, asset::AssetType::KPAT_Level, normalized))
            {
                return false;
            }
            return normalized != "level" && normalized.rfind("level/", 0) == 0;
        }

        // A Live2D product is not one of the AssetType extensions, so the
        // generic normalizer cannot classify it. Apply the same containment
        // rules here -- no NUL, no absolute or drive-rooted path, no escaping
        // segment -- and then require the product's own suffix.
        constexpr std::string_view kLive2DProductSuffix = ".live2d";

        bool ParseLive2DModel(const std::string_view value, std::string &normalized)
        {
            const std::string authored{value};
            if (authored.empty() || authored.find('\0') != std::string::npos)
            {
                return false;
            }

            std::string portable = authored;
            std::replace(portable.begin(), portable.end(), '\\', '/');
            if (portable.front() == '/' ||
                (portable.size() >= 2u &&
                 std::isalpha(static_cast<unsigned char>(portable.front())) != 0 &&
                 portable[1] == ':'))
            {
                return false;
            }

            std::vector<std::string> segments;
            std::size_t begin = 0;
            while (begin <= portable.size())
            {
                const std::size_t end = portable.find('/', begin);
                const std::string segment = portable.substr(
                    begin, end == std::string::npos ? std::string::npos : end - begin);
                if (segment == "..")
                {
                    return false;
                }
                if (!segment.empty() && segment != ".")
                {
                    if (segment.find(':') != std::string::npos)
                    {
                        return false;
                    }
                    segments.push_back(segment);
                }
                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1u;
            }

            if (segments.empty())
            {
                return false;
            }
            normalized = segments.front();
            for (std::size_t index = 1u; index < segments.size(); ++index)
            {
                normalized += "/" + segments[index];
            }
            return normalized.size() > kLive2DProductSuffix.size() &&
                   normalized.compare(normalized.size() - kLive2DProductSuffix.size(),
                                      kLive2DProductSuffix.size(),
                                      kLive2DProductSuffix) == 0;
        }

        RuntimeLaunchOptionsParseResult ParseArguments(
            const std::vector<std::string_view> &arguments)
        {
            RuntimeLaunchOptionsParseResult result{};
            bool has_agent_port = false;
            bool has_graphics_api = false;
            bool has_mode = false;
            bool has_startup_level = false;
            bool has_startup_capture = false;
            bool has_capture_view = false;
            bool has_capture_alpha = false;
            bool has_exit_after_capture = false;
            bool has_resize = false;
            bool has_live2d_model = false;

            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                const std::string_view argument = arguments[index];
                if (argument == "--mode")
                {
                    if (has_mode)
                    {
                        return Failure("duplicate option '--mode'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure("--mode requires scene3d or live2d-viewer");
                    }

                    const std::string_view value = arguments[++index];
                    const std::optional<ApplicationMode> mode = ParseApplicationMode(value);
                    if (!mode.has_value())
                    {
                        return Failure("--mode requires scene3d or live2d-viewer (got '" +
                                       std::string{value} + "')");
                    }
                    result.options.application_mode = *mode;
                    has_mode = true;
                }
                else if (argument == "--agent-port")
                {
                    if (has_agent_port)
                    {
                        return Failure("duplicate option '--agent-port'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure("--agent-port requires a port from 1 to 65535");
                    }

                    uint16_t port = 0;
                    const std::string_view value = arguments[++index];
                    if (!ParsePort(value, port))
                    {
                        return Failure("--agent-port requires a port from 1 to 65535 (got '" +
                                       std::string{value} + "')");
                    }
                    result.options.command_transport_config.enabled = true;
                    result.options.command_transport_config.port = port;
                    // Opting into the agent port is what authorizes the mutating
                    // commands reachable through it; without this the transport
                    // grants no capabilities and every MutatesState command is
                    // denied. Read-only commands ignore the extra capability.
                    result.options.command_transport_config.capabilities =
                        command::CommandCapability::Mutating;
                    has_agent_port = true;
                }
                else if (argument == "--graphics-api")
                {
                    if (has_graphics_api)
                    {
                        return Failure("duplicate option '--graphics-api'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure("--graphics-api requires vulkan or opengl");
                    }

                    const std::string_view value = arguments[++index];
                    if (value == "vulkan")
                    {
                        result.options.graphics_api_type =
                            GraphicsAPIType::GRAPHICS_API_VULKAN;
                    }
                    else if (value == "opengl")
                    {
                        result.options.graphics_api_type =
                            GraphicsAPIType::GRAPHICS_API_OPENGL;
                    }
                    else
                    {
                        return Failure("--graphics-api requires vulkan or opengl (got '" +
                                       std::string{value} + "')");
                    }
                    has_graphics_api = true;
                }
                else if (argument == "--startup-level")
                {
                    if (has_startup_level)
                    {
                        return Failure("duplicate option '--startup-level'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure(
                            "--startup-level requires an Asset-root-relative level/*.level path");
                    }

                    std::string normalized;
                    const std::string_view value = arguments[++index];
                    if (!ParseStartupLevel(value, normalized))
                    {
                        return Failure(
                            "--startup-level requires an Asset-root-relative level/*.level path (got '" +
                            std::string{value} + "')");
                    }
                    result.options.startup_level_override = std::move(normalized);
                    has_startup_level = true;
                }
                else if (argument == "--live2d-model")
                {
                    if (has_live2d_model)
                    {
                        return Failure("duplicate option '--live2d-model'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure(
                            "--live2d-model requires an Asset-root-relative *.live2d path");
                    }

                    std::string normalized;
                    const std::string_view value = arguments[++index];
                    if (!ParseLive2DModel(value, normalized))
                    {
                        return Failure(
                            "--live2d-model requires an Asset-root-relative *.live2d path (got '" +
                            std::string{value} + "')");
                    }
                    result.options.live2d_model_override = std::move(normalized);
                    has_live2d_model = true;
                }
                else if (argument == "--capture")
                {
                    if (has_startup_capture)
                    {
                        return Failure("duplicate option '--capture'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure(
                            "--capture requires a relative save/screenshots/validation/*.png path");
                    }

                    result.options.startup_capture_override = std::string{arguments[++index]};
                    has_startup_capture = true;
                }
                else if (argument == "--capture-view")
                {
                    if (has_capture_view)
                    {
                        return Failure("duplicate option '--capture-view'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure("--capture-view requires window or live2d");
                    }

                    const std::string_view value = arguments[++index];
                    if (value == "window")
                    {
                        result.options.startup_capture_view =
                            StartupCaptureView::Presentation;
                    }
                    else if (value == "live2d")
                    {
                        result.options.startup_capture_view = StartupCaptureView::Product;
                    }
                    else
                    {
                        return Failure("--capture-view requires window or live2d (got '" +
                                       std::string{value} + "')");
                    }
                    has_capture_view = true;
                }
                else if (argument == "--capture-alpha")
                {
                    if (has_capture_alpha)
                    {
                        return Failure("duplicate option '--capture-alpha'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure("--capture-alpha requires opaque or transparent");
                    }

                    const std::string_view value = arguments[++index];
                    if (value == "opaque")
                    {
                        result.options.startup_capture_transparent_clear = false;
                    }
                    else if (value == "transparent")
                    {
                        result.options.startup_capture_transparent_clear = true;
                    }
                    else
                    {
                        return Failure(
                            "--capture-alpha requires opaque or transparent (got '" +
                            std::string{value} + "')");
                    }
                    has_capture_alpha = true;
                }
                else if (argument == "--exit-after-capture")
                {
                    if (has_exit_after_capture)
                    {
                        return Failure("duplicate option '--exit-after-capture'");
                    }
                    result.options.startup_exit_after_capture = true;
                    has_exit_after_capture = true;
                }
                else if (argument == "--resize")
                {
                    if (has_resize)
                    {
                        return Failure("duplicate option '--resize'");
                    }
                    if (HasMissingValue(arguments, index))
                    {
                        return Failure("--resize requires WIDTHxHEIGHT");
                    }

                    const std::string_view value = arguments[++index];
                    RuntimeResizeRequest resize{};
                    if (!ParseResizeExtent(value, resize))
                    {
                        return Failure(
                            "--resize requires WIDTHxHEIGHT with both values from 1 to " +
                            std::to_string(kMaximumResizeExtent) + " (got '" +
                            std::string{value} + "')");
                    }
                    result.options.startup_resize = resize;
                    has_resize = true;
                }
                else
                {
                    return Failure("unknown option '" + std::string{argument} + "'");
                }
            }

            if (result.options.application_mode == ApplicationMode::Live2DViewer)
            {
                if (result.options.startup_level_override.has_value())
                {
                    return Failure("--startup-level is only valid in scene3d mode");
                }
                if (has_capture_view && !has_startup_capture)
                {
                    return Failure("--capture-view requires --capture");
                }
                if (has_capture_alpha && !has_startup_capture)
                {
                    return Failure("--capture-alpha requires --capture");
                }
                if (has_exit_after_capture && !has_startup_capture)
                {
                    return Failure("--exit-after-capture requires --capture");
                }
            }
            else
            {
                if (result.options.startup_capture_override.has_value())
                {
                    return Failure("--capture is only valid in live2d-viewer mode");
                }
                if (has_capture_view)
                {
                    return Failure("--capture-view is only valid in live2d-viewer mode");
                }
                if (has_capture_alpha)
                {
                    return Failure("--capture-alpha is only valid in live2d-viewer mode");
                }
                if (has_exit_after_capture)
                {
                    return Failure(
                        "--exit-after-capture is only valid in live2d-viewer mode");
                }
                if (has_resize)
                {
                    return Failure("--resize is only valid in live2d-viewer mode");
                }
                if (has_live2d_model)
                {
                    return Failure(
                        "--live2d-model is only valid in live2d-viewer mode");
                }
            }

            result.succeeded = true;
            return result;
        }
    }

    RuntimeLaunchOptionsParseResult ParseRuntimeLaunchOptions(
        const std::vector<std::string_view> &arguments)
    {
        return ParseArguments(arguments);
    }

    RuntimeLaunchOptionsParseResult ParseRuntimeLaunchOptions(int argc, char **argv)
    {
        if (argc < 0 || argv == nullptr)
        {
            return Failure("invalid process argument array");
        }

        std::vector<std::string_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] == nullptr)
            {
                return Failure("invalid null process argument");
            }
            arguments.emplace_back(argv[index]);
        }
        return ParseArguments(arguments);
    }
}
