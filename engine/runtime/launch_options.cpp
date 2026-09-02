#include "launch_options.h"

#include <charconv>
#include <string>
#include <utility>

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

        RuntimeLaunchOptionsParseResult ParseArguments(
            const std::vector<std::string_view> &arguments)
        {
            RuntimeLaunchOptionsParseResult result{};
            bool has_agent_port = false;
            bool has_graphics_api = false;
            bool has_startup_level = false;

            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                const std::string_view argument = arguments[index];
                if (argument == "--agent-port")
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
                else
                {
                    return Failure("unknown option '" + std::string{argument} + "'");
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
