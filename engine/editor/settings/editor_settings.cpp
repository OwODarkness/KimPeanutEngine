#include "editor/settings/editor_settings.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include "log/logger.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr int kSettingsVersion = 1;

        // Minimal whole-file read, mirroring bootstrap's ReadText. Throws
        // std::runtime_error on a missing file so ReadEditorSettings fails fast.
        std::string ReadText(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("settings: failed to open " + path);
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        // Maps a level name in settings.json to its LogLevel index, or -1 for an
        // unknown name (ignored with a warning rather than fatal).
        int LevelIndexFromName(const std::string &name)
        {
            if (name == "debug") return static_cast<int>(program::LogLevel::Debug);
            if (name == "info") return static_cast<int>(program::LogLevel::Info);
            if (name == "warning") return static_cast<int>(program::LogLevel::Warning);
            if (name == "error") return static_cast<int>(program::LogLevel::Error);
            if (name == "fatal") return static_cast<int>(program::LogLevel::Fatal);
            return -1;
        }

        // A color is a 4-element number array; anything else keeps the fallback.
        LogColor ParseColor(const nlohmann::json &node, const LogColor &fallback)
        {
            if (!node.is_array() || node.size() != 4)
            {
                return fallback;
            }
            for (const auto &channel : node)
            {
                if (!channel.is_number())
                {
                    return fallback;
                }
            }
            LogColor color;
            color.r = node[0].get<float>();
            color.g = node[1].get<float>();
            color.b = node[2].get<float>();
            color.a = node[3].get<float>();
            return color;
        }
    }

    LogLevelColorTable DefaultLogColors()
    {
        LogLevelColorTable table;
        table[static_cast<size_t>(program::LogLevel::Debug)] = {1.f, 1.f, 1.f, 1.f};
        table[static_cast<size_t>(program::LogLevel::Info)] = {1.f, 1.f, 1.f, 1.f};
        table[static_cast<size_t>(program::LogLevel::Warning)] = {1.f, 1.f, 0.f, 1.f};
        table[static_cast<size_t>(program::LogLevel::Error)] = {0.5f, 0.2f, 0.f, 1.f};
        table[static_cast<size_t>(program::LogLevel::Fatal)] = {1.f, 0.f, 0.f, 1.f};
        return table;
    }

    EditorSettings ReadEditorSettings(const std::string &path)
    {
        EditorSettings settings;
        settings.log_colors = DefaultLogColors();

        // ReadText throws std::runtime_error if the file is missing.
        nlohmann::json json = nlohmann::json::parse(ReadText(path));

        settings.version = json.value("version", kSettingsVersion);
        if (settings.version != kSettingsVersion)
        {
            KP_LOG("LogEditorSettings", LOG_LEVEL_WARNING,
                   "%s uses settings version %d, editor supports %d",
                   path.c_str(), settings.version, kSettingsVersion);
        }

        if (json.contains("background_color"))
        {
            settings.background_color = ParseColor(json["background_color"],
                                                   settings.background_color);
        }

        if (!json.contains("log_colors"))
        {
            return settings;
        }
        if (!json["log_colors"].is_object())
        {
            KP_LOG("LogEditorSettings", LOG_LEVEL_WARNING,
                   "%s: \"log_colors\" is not an object, ignoring", path.c_str());
            return settings;
        }

        for (const auto &[name, node] : json["log_colors"].items())
        {
            const int level = LevelIndexFromName(name);
            if (level < 0)
            {
                KP_LOG("LogEditorSettings", LOG_LEVEL_WARNING,
                       "%s: ignoring unknown log level \"%s\"", path.c_str(), name.c_str());
                continue;
            }
            const size_t index = static_cast<size_t>(level);
            settings.log_colors[index] = ParseColor(node, settings.log_colors[index]);
        }

        return settings;
    }
}
