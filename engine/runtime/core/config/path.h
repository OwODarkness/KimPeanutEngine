#ifndef KPENGINE_PLATFORM_PATH_H
#define KPENGINE_PLATFORM_PATH_H

#include <string>
#include <filesystem>

#include "global.h"

namespace kpengine
{
    inline const std::filesystem::path project_root = PROJECT_ROOT_DIR;
    inline const std::filesystem::path binary_root = PROJECT_BINARY_DIR;

    // Compose <root>/<relative> into a portable string. Preserves any trailing
    // slash carried by `relative`, so the per-type getters keep their exact
    // (trailing-slash) return values.
    inline std::string ComposePath(const std::filesystem::path& root, const char* relative)
    {
        return (root / relative).generic_string();
    }

    inline std::string GetAssetDirectory()
    {
        return ComposePath(project_root, "asset/");
    }

    inline std::string GetBootstrapPath()
    {
        return ComposePath(project_root, "config/bootstrap.json");
    }

    inline std::string GetIconPath()
    {
        return ComposePath(project_root, "config/icon.png");
    }

    inline std::string GetSettingsPath()
    {
        return ComposePath(project_root, "config/settings.json");
    }

    inline std::string GetTextureDirectory()
    {
        return ComposePath(project_root, "asset/texture/");
    }

    inline std::string GetModelDirectory()
    {
        return ComposePath(project_root, "asset/model/");
    }

    inline std::string GetShaderDirectory()
    {
        return ComposePath(project_root, "asset/shader/");
    }

    inline std::string GetScriptDirectory()
    {
        return ComposePath(project_root, "asset/script/");
    }

    inline std::string GetSPVShaderDirectory()
    {
        return ComposePath(binary_root, "shaders/");
    }

    inline std::string GetLogDirectory()
    {
        return ComposePath(project_root, "logs/");
    }
}

#endif // KPENGINE_PLATFORM_PATH_H
