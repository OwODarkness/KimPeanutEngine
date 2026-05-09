#include "shader_cache.h"
#include <filesystem>
#include <sstream>
#include "config/path.h"
#include "log/logger.h"
#include "utility.h"
namespace kpengine::resource{
    void ShaderCache::Initialize(GraphicsAPIType api_type)
    {
        api_ = api_type;

        switch (api_type)
        {
        case GraphicsAPIType::GRAPHICS_API_VULKAN:
            extension_ = ".spv";
            directory_ = GetShaderDirectory() + "/cache/vulkan/";
            break;
        
        default:
            break;
        }
    }

    bool ShaderCache::Has(uint64_t hash) const
    {
        if(memory_cache_.count(hash))
            return true;

        return std::filesystem::exists(GetPath(hash));
    }

    const std::vector<uint8_t>& ShaderCache::Load(uint64_t hash)
    {
        auto it = memory_cache_.find(hash);
        if(it != memory_cache_.end())
        {
            return it->second;
        }

        auto& ref = memory_cache_[hash];
        ref = std::move(ReadBinary(GetPath(hash)));
        return ref;
    }

    void ShaderCache::Save( uint64_t hash, const std::vector<uint8_t>& binary)
    {
        memory_cache_[hash] = binary;

        std::filesystem::create_directories(directory_);
        std::string file_path = GetPath(hash);
        std::ofstream file(file_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(binary.data()), binary.size() * sizeof(uint8_t));
    }

    std::string ShaderCache::GetPath(uint64_t hash) const
    {
        std::stringstream ss;
        ss << directory_ << std::hex << hash << extension_;
        return ss.str();
    }
}