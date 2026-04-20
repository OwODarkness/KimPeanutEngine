#include "shader_cache.h"
#include <filesystem>
#include <sstream>
#include "config/path.h"
#include "log/logger.h"
#include "utility.h"
namespace kpengine::resource{
    void ShaderCache::Initialize(GraphicsAPIType api_type)
    {
        api = api_type;

        switch (api_type)
        {
        case GraphicsAPIType::GRAPHICS_API_VULKAN:
            extension = ".spv";
            directory = GetShaderDirectory() + "/cache/vulkan/";
            break;
        
        default:
            break;
        }
    }

    bool ShaderCache::Has(uint64_t hash) const
    {
        return std::filesystem::exists(GetPath(hash));
    }

    std::vector<uint8_t> ShaderCache::Load(uint64_t hash)
    {
        return ReadBinary(GetPath(hash));
    }

    void ShaderCache::Save( uint64_t hash, const std::vector<uint8_t>& binary)
    {
        std::filesystem::create_directories(directory);
        std::string file_path = GetPath(hash);
        std::ofstream file(file_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(binary.data()), binary.size() * sizeof(uint8_t));
    }

    std::string ShaderCache::GetPath(uint64_t hash) const
    {
        std::stringstream ss;
        ss << directory << std::hex << hash << extension;
        return ss.str();
    }
}