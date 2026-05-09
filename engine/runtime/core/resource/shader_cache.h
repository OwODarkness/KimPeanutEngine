#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_CACHE_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_CACHE_H

#include "base/type.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
namespace kpengine::resource{
    class ShaderCache{
    public:
        void Initialize(GraphicsAPIType api_type);
        bool Has(uint64_t hash) const;
        void Save( uint64_t hash, const std::vector<uint8_t>& binary);
        const std::vector<uint8_t>& Load(uint64_t hash);
    private:
        std::string GetPath(uint64_t) const;
    private:
        std::unordered_map<uint64_t, std::vector<uint8_t>> memory_cache_;
        GraphicsAPIType api_;
        std::string extension_;
        std::string directory_;
    };
}

#endif