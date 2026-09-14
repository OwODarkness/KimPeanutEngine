#include "utility.h"
#include "log/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <utility>

namespace kpengine::resource
{
    std::string ReadText(const std::string &path)
    {
        std::ifstream file(path, std::ios::in);
        if (!file.is_open())
        {
            KP_LOG("ReadTextLog", LOG_LEVEL_ERROR, "Failed to open text file: " + path);
            throw std::runtime_error("Failed to open text file: " + path);
        }

        return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }
    std::vector<uint8_t> ReadBinary(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            KP_LOG("ReadBinaryLog", LOG_LEVEL_ERROR,
                   "failed to open binary file: " + path);
            throw std::runtime_error("Failed to open binary file: " + path);
        }

        std::streamsize file_size = file.tellg();
        if (file_size <= 0)
        {
            KP_LOG("ReadBinaryLog", LOG_LEVEL_ERROR,
                   "invalid file size: " + path);
            throw std::runtime_error("Invalid file size: " + path);
        }

        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(file_size));

        if (!file.read(reinterpret_cast<char *>(buffer.data()), file_size))
        {
            KP_LOG("ReadBinaryLog", LOG_LEVEL_ERROR,
                   "Failed to read binary file: " + path);
            throw std::runtime_error("Failed to read binary file: " + path);
        }

        return buffer;
    }

    MurmurHash128 MurmurHash3_x64_128(
        std::span<const std::byte> key, std::uint32_t seed)
    {
        const std::byte *data = key.data();
        const size_t length = key.size();
        const size_t nblocks = length / 16;

        uint64_t h1 = seed;
        uint64_t h2 = seed;

        const uint64_t c1 = 0x87c37b91114253d5ULL;
        const uint64_t c2 = 0x4cf5ad432745937fULL;

        for (size_t i = 0; i < nblocks; i++)
        {
            uint64_t k1{};
            uint64_t k2{};
            std::memcpy(&k1, data + i * 16, sizeof(k1));
            std::memcpy(&k2, data + i * 16 + sizeof(k1), sizeof(k2));

            k1 *= c1;
            k1 = (k1 << 31) | (k1 >> (64 - 31));
            k1 *= c2;
            h1 ^= k1;
            h1 = (h1 << 27) | (h1 >> (64 - 27));
            h1 += h2;
            h1 = h1 * 5 + 0x52dce729;

            k2 *= c2;
            k2 = (k2 << 33) | (k2 >> (64 - 33));
            k2 *= c1;
            h2 ^= k2;
            h2 = (h2 << 31) | (h2 >> (64 - 31));
            h2 += h1;
            h2 = h2 * 5 + 0x38495ab5;
        }

        // tail
        const auto tail = key.subspan(nblocks * 16);
        uint64_t k1 = 0;
        uint64_t k2 = 0;

        switch (length & 15)
        {
        case 15:
            k2 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[14]))) << 48;
            [[fallthrough]];
        case 14:
            k2 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[13]))) << 40;
            [[fallthrough]];
        case 13:
            k2 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[12]))) << 32;
            [[fallthrough]];
        case 12:
            k2 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[11]))) << 24;
            [[fallthrough]];
        case 11:
            k2 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[10]))) << 16;
            [[fallthrough]];
        case 10:
            k2 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[9]))) << 8;
            [[fallthrough]];
        case 9:
            k2 ^= static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[8]));
            k2 *= c2;
            k2 = (k2 << 33) | (k2 >> (64 - 33));
            k2 *= c1;
            h2 ^= k2;
            [[fallthrough]];
        case 8:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[7]))) << 56;
            [[fallthrough]];
        case 7:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[6]))) << 48;
            [[fallthrough]];
        case 6:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[5]))) << 40;
            [[fallthrough]];
        case 5:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[4]))) << 32;
            [[fallthrough]];
        case 4:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[3]))) << 24;
            [[fallthrough]];
        case 3:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[2]))) << 16;
            [[fallthrough]];
        case 2:
            k1 ^= (static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[1]))) << 8;
            [[fallthrough]];
        case 1:
            k1 ^= static_cast<uint64_t>(std::to_integer<std::uint8_t>(tail[0]));
            k1 *= c1;
            k1 = (k1 << 31) | (k1 >> (64 - 31));
            k1 *= c2;
            h1 ^= k1;
        };

        h1 ^= static_cast<uint64_t>(length);
        h2 ^= static_cast<uint64_t>(length);
        h1 += h2;
        h2 += h1;

        auto fmix = [](uint64_t k)
        {
            k ^= k >> 33;
            k *= 0xff51afd7ed558ccdULL;
            k ^= k >> 33;
            k *= 0xc4ceb9fe1a85ec53ULL;
            k ^= k >> 33;
            return k;
        };

        h1 = fmix(h1);
        h2 = fmix(h2);
        h1 += h2;
        h2 += h1;

        return {h1, h2};
    }

    uint64_t GenerateShaderHash(
        std::string_view content,
        std::string_view stage,
        std::string_view entry,
        std::vector<std::string> macro_defines)
    {
        std::sort(macro_defines.begin(), macro_defines.end());
        std::string input;
        input.reserve(content.size() + 256);

        input += stage;
        input += '|';
        input += entry;
        input += '|';

        for (const auto &m : macro_defines)
        {
            input += m;
            input += ';';
        }

        input += '|';
        input += content;

        const auto input_bytes = std::as_bytes(
            std::span<const char>{input.data(), input.size()});
        const MurmurHash128 out = MurmurHash3_x64_128(input_bytes, 0);

        return out[0];
    }
}
