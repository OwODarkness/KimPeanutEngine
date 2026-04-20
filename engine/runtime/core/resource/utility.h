#ifndef KPENGINE_RUNTIME_RESOURCE_UTILITY_H
#define KPENGINE_RUNTIME_RESOURCE_UTILITY_H

#include <string>
#include <vector>
#include <cstdint>

namespace kpengine::resource
{
    std::string ReadText(const std::string &path);
    std::vector<uint8_t> ReadBinary(const std::string& path);

    void MurmurHash3_x64_128(const void *key, const int len, uint32_t seed, uint64_t out[2]);

    uint64_t GenerateShaderHash(
        const std::string &content,
        const std::string &stage,
        const std::string &entry,
        std::vector<std::string> macro_defines);

    
}

#endif