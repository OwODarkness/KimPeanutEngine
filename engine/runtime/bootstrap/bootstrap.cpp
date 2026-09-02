#include "bootstrap/bootstrap.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "asset/utility.h"
#include "log/logger.h"

namespace kpengine::bootstrap
{
    namespace
    {
        constexpr int kBootstrapVersion = 2;

        // Minimal whole-file read. Throws std::runtime_error if the file is missing
        // so ReadBootstrap can fail fast at boot (surfaced by main's try/catch).
        std::string ReadText(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("bootstrap: failed to open " + path);
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    BootstrapConfig ReadBootstrap(const std::string &path)
    {
        const nlohmann::json source = nlohmann::json::parse(ReadText(path));
        const auto fail = [&path](const char *reason) -> void
        {
            KP_LOG("BootstrapLog", LOG_LEVEL_ERROR, "%s: %s", path.c_str(), reason);
            throw std::runtime_error("bootstrap: " + path + ": " + reason);
        };

        if (!source.is_object())
        {
            fail("root must be an object");
        }
        for (const auto &[name, value] : source.items())
        {
            (void)value;
            if (name != "version" && name != "startup_level")
            {
                fail("unknown field in Bootstrap V2");
            }
        }
        if (!source.contains("version") || !source["version"].is_number_integer() ||
            source["version"].get<int>() != kBootstrapVersion)
        {
            fail("unsupported or missing version");
        }
        if (!source.contains("startup_level") || !source["startup_level"].is_string())
        {
            fail("startup_level must be a non-empty .level path");
        }

        BootstrapConfig config{};
        config.version = kBootstrapVersion;
        if (!asset::NormalizeAssetRootRelativePath(
                source["startup_level"].get<std::string>(), asset::AssetType::KPAT_Level,
                config.startup_level) ||
            (config.startup_level != "level" &&
             config.startup_level.rfind("level/", 0) != 0))
        {
            fail("startup_level must be an Asset-root-relative level/*.level path");
        }
        return config;
    }
}
