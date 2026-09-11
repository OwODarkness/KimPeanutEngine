#include "live2d_settings.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <nlohmann/json.hpp>

#include "asset/utility.h"
#include "log/logger.h"

namespace kpengine::live2d
{
    namespace
    {
        constexpr int kLive2DSettingsVersion = 1;

        std::string ReadText(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("live2d settings: failed to open " + path);
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    Live2DSettings ReadLive2DSettings(const std::string &path)
    {
        const nlohmann::json source = nlohmann::json::parse(ReadText(path));
        const auto fail = [&path](const char *reason) -> void
        {
            KP_LOG("Live2DModule", LOG_LEVEL_ERROR, "%s: %s", path.c_str(), reason);
            throw std::runtime_error("live2d settings: " + path + ": " + reason);
        };

        if (!source.is_object())
        {
            fail("root must be an object");
        }
        for (const auto &[name, value] : source.items())
        {
            (void)value;
            if (name != "version" && name != "enabled" && name != "preview_asset")
            {
                fail("unknown field");
            }
        }
        if (!source.contains("version") || !source["version"].is_number_integer() ||
            source["version"].get<int>() != kLive2DSettingsVersion)
        {
            fail("unsupported or missing version");
        }
        if (!source.contains("enabled") || !source["enabled"].is_boolean())
        {
            fail("enabled must be a boolean");
        }

        Live2DSettings settings{};
        settings.version = kLive2DSettingsVersion;
        settings.enabled = source["enabled"].get<bool>();
        if (!settings.enabled)
        {
            if (source.contains("preview_asset") && !source["preview_asset"].is_string())
            {
                fail("preview_asset must be an Asset-root-relative .live2d path");
            }
            return settings;
        }

        if (!source.contains("preview_asset") || !source["preview_asset"].is_string())
        {
            fail("preview_asset must be an Asset-root-relative .live2d path");
        }
        std::string normalized;
        // The .live2d type is module-owned, so Asset core cannot classify it;
        // Undefined here asks only for root-relative path normalization.
        if (!asset::NormalizeAssetRootRelativePath(
                source["preview_asset"].get<std::string>(), asset::AssetType::Undefined,
                normalized) || asset::GetFileExtension(normalized) != "live2d")
        {
            fail("preview_asset must be an Asset-root-relative .live2d path");
        }
        settings.preview_asset = std::move(normalized);
        return settings;
    }
}
