#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_CACHE_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_CACHE_H

#include "base/type.h"
#include <string>
#include <vector>
#include <cstdint>
namespace kpengine::resource{
    class ShaderCache{
    public:
        void Initialize(GraphicsAPIType api_type);
        bool Has(uint64_t hash) const;
        void Save( uint64_t hash, const std::vector<uint8_t>& binary);
        std::vector<uint8_t> Load(uint64_t hash);
    private:
        std::string GetPath(uint64_t) const;
    private:
        GraphicsAPIType api;
        std::string extension;
        std::string directory;
    };
}

#endif