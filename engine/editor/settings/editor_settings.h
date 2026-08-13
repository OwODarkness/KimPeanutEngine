#ifndef KPENGINE_EDITOR_SETTINGS_H
#define KPENGINE_EDITOR_SETTINGS_H

#include <array>
#include <string>

#include "log/log_entry.h"

namespace kpengine::editor
{
    // RGBA color (0..1). Plain floats keep the parser and its tests imgui-free.
    struct LogColor
    {
        float r = 1.f;
        float g = 1.f;
        float b = 1.f;
        float a = 1.f;
    };

    // One color per program::LogLevel (Debug..Fatal); index with static_cast<size_t>(level).
    using LogLevelColorTable = std::array<LogColor, 5>;

    struct EditorSettings
    {
        int version = 1;
        LogLevelColorTable log_colors;
    };

    // The hard-coded colors the log window used to switch on; the fallback when
    // settings.json omits a level or can't be read.
    LogLevelColorTable DefaultLogColors();

    // Parses config/settings.json into EditorSettings. Throws std::runtime_error if
    // the file is missing (mirrors ReadBootstrap) and propagates nlohmann parse
    // errors on malformed JSON; missing/unknown "log_colors" entries fall back to
    // DefaultLogColors() with a warning.
    EditorSettings ReadEditorSettings(const std::string &path);
}

#endif // KPENGINE_EDITOR_SETTINGS_H
