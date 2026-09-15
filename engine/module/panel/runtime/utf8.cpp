#include "utf8.h"

#include <cstddef>

namespace kpengine::panel
{
    namespace
    {
        // Length implied by a lead byte, or 0 when the byte can never start a
        // sequence. The ranges are narrowed here so the checks below only have
        // to handle overlong forms, surrogates, and values past U+10FFFF.
        std::uint32_t LeadByteLength(std::uint8_t lead) noexcept
        {
            if (lead < 0x80u)
            {
                return 1u;
            }
            if (lead >= 0xC2u && lead <= 0xDFu)
            {
                return 2u;
            }
            if (lead >= 0xE0u && lead <= 0xEFu)
            {
                return 3u;
            }
            if (lead >= 0xF0u && lead <= 0xF4u)
            {
                return 4u;
            }
            return 0u;
        }

        std::uint32_t LeadByteBits(std::uint32_t length) noexcept
        {
            switch (length)
            {
            case 1u:
                return 0x7Fu;
            case 2u:
                return 0x1Fu;
            case 3u:
                return 0x0Fu;
            case 4u:
                return 0x07u;
            default:
                return 0u;
            }
        }
    }

    std::u32string DecodeUtf8(std::string_view utf8)
    {
        std::u32string codepoints;
        codepoints.reserve(utf8.size());

        std::size_t index = 0u;
        while (index < utf8.size())
        {
            const auto lead = static_cast<std::uint8_t>(utf8[index]);
            const std::uint32_t length = LeadByteLength(lead);
            if (length == 0u)
            {
                codepoints.push_back(kReplacementCodepoint);
                ++index;
                continue;
            }

            std::uint32_t value = lead & LeadByteBits(length);
            bool complete = true;
            for (std::uint32_t offset = 1u; offset < length; ++offset)
            {
                if (index + offset >= utf8.size())
                {
                    complete = false;
                    break;
                }
                const auto continuation =
                    static_cast<std::uint8_t>(utf8[index + offset]);
                if ((continuation & 0xC0u) != 0x80u)
                {
                    complete = false;
                    break;
                }
                value = (value << 6u) | (continuation & 0x3Fu);
            }

            const bool overlong = (length == 2u && value < 0x80u) ||
                                  (length == 3u && value < 0x800u) ||
                                  (length == 4u && value < 0x10000u);
            const bool surrogate = value >= 0xD800u && value <= 0xDFFFu;
            if (!complete || overlong || surrogate || value > 0x10FFFFu)
            {
                // Advance exactly one byte so the next lead byte is reconsidered
                // rather than skipped.
                codepoints.push_back(kReplacementCodepoint);
                ++index;
                continue;
            }

            codepoints.push_back(static_cast<char32_t>(value));
            index += length;
        }

        return codepoints;
    }
}
