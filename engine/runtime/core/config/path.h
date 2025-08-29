#ifndef KPENGINE_PLATFORM_PATH_H
#define KPENGINE_PLATFORM_PATH_H

#include <string>
#include <filesystem>

#include "global.h"

namespace kpengine
{
    inline const std::filesystem::path project_root =PROJECT_ROOT_DIR;
    inline const std::filesystem::path binary_root = PROJECT_BINARY_DIR;

    inline std::string GetAssetDirectory()
    {
        return (project_root / "engine/asset/").generic_string();
    }

    inline std::string GetTextureDirectory()
    {
        return (project_root / "engine/asset/texture/").generic_string();
    }

    inline std::string GetModelDirectory()
    {
        return (project_root / "engine/asset/model/").generic_string();
    }

    inline std::string GetShaderDirectory()
    {
        return (project_root / "engine/shader/").generic_string();
    }

    inline std::string GetSPVShaderDirectory()
    {
        return (binary_root / "shaders/").generic_string();        
    }

    inline std::string GetLogDirectory()
    {
        return (project_root / "logs/").generic_string();
    }
}

#endif // KPENGINE_PLATFORM_PATH_H
