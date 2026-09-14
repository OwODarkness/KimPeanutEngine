#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>

#include "resource/utility.h"

namespace
{
    TEST(ResourceTest, MurmurHashEmptyInputIsStable)
    {
        const auto hash = kpengine::resource::MurmurHash3_x64_128({}, 0);

        EXPECT_EQ(hash[0], 0ULL);
        EXPECT_EQ(hash[1], 0ULL);
    }

    TEST(ResourceTest, MurmurHashPreservesKnownDigest)
    {
        const std::array<std::uint8_t, 5> input{'h', 'e', 'l', 'l', 'o'};
        const auto bytes = std::as_bytes(std::span<const std::uint8_t>{input});
        const auto hash = kpengine::resource::MurmurHash3_x64_128(bytes, 0);

        EXPECT_EQ(hash[0], 0xcbd8a7b341bd9b02ULL);
        EXPECT_EQ(hash[1], 0x5b1e906a48ae1d19ULL);
    }
}
