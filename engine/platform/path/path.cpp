#include "path.h"

namespace kpengine
{
    const std::filesystem::path project_root = PROJECT_ROOT_DIR;
    std::string GetAssetDirectory()
    {
        return (project_root / "engine/asset/").generic_string() ;
    }
    
    std::string GetTextureDirectory()
    {
        return (project_root / "engine/asset/texture/").generic_string();
    }
    std::string GetModelDirectory()
    {
        return (project_root / "engine/asset/model/").generic_string();
    }
    std::string GetShaderDirectory()
    {
        return (project_root / "engine/shader/").generic_string();
    }
    std::string GetLogDirectory()
    {
        return (project_root / "logs/").generic_string();
    }
}