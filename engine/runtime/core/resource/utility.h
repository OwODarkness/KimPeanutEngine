#ifndef KPENGINE_RUNTIME_RESOURCE_UTILITY_H
#define KPENGINE_RUNTIME_RESOURCE_UTILITY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kpengine::resource
{
    std::string ReadText(const std::string &path);
    std::vector<uint8_t> ReadBinary(const std::string& path);

    using MurmurHash128 = std::array<std::uint64_t, 2>;

    MurmurHash128 MurmurHash3_x64_128(
        std::span<const std::byte> key, std::uint32_t seed);

    uint64_t GenerateShaderHash(
        std::string_view content,
        std::string_view stage,
        std::string_view entry,
        std::vector<std::string> macro_defines);

    
}

#endif
