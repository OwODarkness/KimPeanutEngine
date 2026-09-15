#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "utf8.h"

namespace kpengine::panel
{
    namespace
    {
        // Both the byte sequences and the expected codepoints are written as
        // escapes or numeric values, so the test does not depend on this
        // file's own encoding.
        constexpr std::string_view kNihao = "\xE4\xBD\xA0\xE5\xA5\xBD"; // U+4F60 U+597D
        constexpr char32_t kNihaoFirst = static_cast<char32_t>(0x4F60u);
        constexpr char32_t kNihaoSecond = static_cast<char32_t>(0x597Du);
        constexpr std::size_t kNihaoCount = 2u;

        std::u32string Decode(const std::string_view bytes)
        {
            return DecodeUtf8(bytes);
        }

        std::u32string Replacement(const std::size_t count)
        {
            return std::u32string(count, kReplacementCodepoint);
        }
    }

    TEST(Utf8Test, DecodesNothingFromEmptyInput)
    {
        EXPECT_TRUE(Decode("").empty());
    }

    TEST(Utf8Test, DecodesAscii)
    {
        EXPECT_EQ(Decode("OvO"), (std::u32string{U'O', U'v', U'O'}));
    }

    TEST(Utf8Test, DecodesATwoByteSequence)
    {
        // C3 A9 is U+00E9 LATIN SMALL LETTER E WITH ACUTE.
        EXPECT_EQ(Decode("\xC3\xA9"),
                  (std::u32string{static_cast<char32_t>(0xE9u)}));
    }

    TEST(Utf8Test, DecodesAThreeByteSequence)
    {
        EXPECT_EQ(Decode("\xE4\xBD\xA0"), (std::u32string{kNihaoFirst}));
    }

    TEST(Utf8Test, DecodesAFourByteSequence)
    {
        // F0 9F 98 80 is U+1F600 GRINNING FACE.
        EXPECT_EQ(Decode("\xF0\x9F\x98\x80"),
                  (std::u32string{static_cast<char32_t>(0x1F600u)}));
    }

    TEST(Utf8Test, KeepsChineseCodepointsDistinct)
    {
        const std::u32string decoded = Decode(kNihao);

        ASSERT_EQ(decoded.size(), kNihaoCount);
        EXPECT_EQ(decoded[0], kNihaoFirst);
        EXPECT_EQ(decoded[1], kNihaoSecond);
    }

    TEST(Utf8Test, DecodesMixedAsciiAndChinese)
    {
        EXPECT_EQ(Decode("A\xE4\xBD\xA0" "B"),
                  (std::u32string{U'A', kNihaoFirst, U'B'}));
    }

    TEST(Utf8Test, ReplacesALoneContinuationByte)
    {
        EXPECT_EQ(Decode("\x80"), Replacement(1u));
    }

    TEST(Utf8Test, ReplacesLeadBytesThatCanNeverStartASequence)
    {
        // 0xC0 and 0xC1 are always overlong, and 0xFF is not a lead byte.
        EXPECT_EQ(Decode("\xC0"), Replacement(1u));
        EXPECT_EQ(Decode("\xC1"), Replacement(1u));
        EXPECT_EQ(Decode("\xFF"), Replacement(1u));
    }

    TEST(Utf8Test, ResynchronizesAfterATruncatedSequence)
    {
        // A three-byte lead followed by one continuation and then ASCII. The
        // decoder must still reach the 'A' rather than abandoning the line.
        const std::u32string decoded = Decode("\xE4\xBD" "A");

        ASSERT_EQ(decoded.size(), 3u);
        EXPECT_EQ(decoded[0], kReplacementCodepoint);
        EXPECT_EQ(decoded[1], kReplacementCodepoint);
        EXPECT_EQ(decoded[2], U'A');
    }

    TEST(Utf8Test, ResynchronizesWhenAContinuationByteIsMissing)
    {
        const std::u32string decoded = Decode("\xE4" "A\xE5\xA5\xBD");

        ASSERT_EQ(decoded.size(), 3u);
        EXPECT_EQ(decoded[0], kReplacementCodepoint);
        EXPECT_EQ(decoded[1], U'A');
        EXPECT_EQ(decoded[2], kNihaoSecond);
    }

    TEST(Utf8Test, RejectsOverlongEncodings)
    {
        // Well-formed continuations carrying a value that fits in fewer bytes.
        EXPECT_EQ(Decode("\xE0\x80\x80"), Replacement(3u));
        EXPECT_EQ(Decode("\xF0\x80\x80\x80"), Replacement(4u));
    }

    TEST(Utf8Test, RejectsSurrogateHalves)
    {
        // ED A0 80 encodes U+D800, which is not a scalar value.
        EXPECT_EQ(Decode("\xED\xA0\x80"), Replacement(3u));
    }

    TEST(Utf8Test, RejectsCodepointsPastTheUnicodeRange)
    {
        // F4 90 80 80 encodes U+110000, one past the last scalar value.
        EXPECT_EQ(Decode("\xF4\x90\x80\x80"), Replacement(4u));
    }

    TEST(Utf8Test, PreservesControlCodepoints)
    {
        // A control byte is still a codepoint; dropping it would shift every
        // character after it.
        const std::u32string decoded = Decode(std::string_view("\x01\x02", 2u));

        ASSERT_EQ(decoded.size(), 2u);
        EXPECT_EQ(decoded[0], static_cast<char32_t>(0x01u));
        EXPECT_EQ(decoded[1], static_cast<char32_t>(0x02u));
    }
}
