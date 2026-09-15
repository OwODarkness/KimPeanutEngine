#ifndef KPENGINE_MODULE_PANEL_GLYPH_CELL_H
#define KPENGINE_MODULE_PANEL_GLYPH_CELL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kpengine::panel
{
    // A glyph is a fixed 16x16 dot matrix. 16x16 is the floor at which complex
    // hanzi stay legible; halfwidth ASCII occupies the left 8 columns and
    // advances 8, which is why the advance is separate from the stored width.
    inline constexpr std::uint32_t kGlyphRows = 16u;
    inline constexpr std::uint32_t kGlyphColumns = 16u;
    inline constexpr std::uint32_t kFullwidthAdvance = kGlyphColumns;
    inline constexpr std::uint32_t kHalfwidthAdvance = kGlyphColumns / 2u;

    struct GlyphCell final
    {
        // Row-major. Bit 0 is the leftmost dot of the row, so the low byte of
        // every row holds the left 8 columns and a byte-aligned stamp is a
        // byte-wise operation.
        std::array<std::uint16_t, kGlyphRows> rows{};

        // Dots the cursor moves after this glyph. Stored with the glyph rather
        // than derived from it because a halfwidth glyph occupies 16 columns of
        // storage while advancing only 8.
        std::uint32_t advance = kFullwidthAdvance;

        bool IsHalfwidth() const noexcept { return advance < kGlyphColumns; }

        bool TestDot(std::uint32_t row, std::uint32_t column) const noexcept
        {
            if (row >= kGlyphRows || column >= kGlyphColumns)
            {
                return false;
            }
            return ((rows[row] >> column) & 1u) != 0u;
        }

        void SetDot(std::uint32_t row, std::uint32_t column, bool on) noexcept
        {
            if (row >= kGlyphRows || column >= kGlyphColumns)
            {
                return;
            }
            const auto bit = static_cast<std::uint16_t>(1u << column);
            rows[row] = on
                            ? static_cast<std::uint16_t>(rows[row] | bit)
                            : static_cast<std::uint16_t>(rows[row] &
                                                         static_cast<std::uint16_t>(~bit));
        }

        // A halfwidth glyph that uses columns 8..15 bleeds into the character
        // after it. The glyph set rejects such a glyph at load time instead of
        // letting it corrupt layout silently.
        bool HasCleanHalfwidth() const noexcept
        {
            if (!IsHalfwidth())
            {
                return true;
            }
            constexpr std::uint16_t kRightHalfMask = 0xFF00u;
            for (const std::uint16_t row : rows)
            {
                if ((row & kRightHalfMask) != 0u)
                {
                    return false;
                }
            }
            return true;
        }
    };

    // Maps codepoints to glyphs over one contiguous range. Entries are fixed
    // size and trivially copyable, so lookup is an index into a flat array
    // rather than an allocation per character.
    class GlyphSet
    {
    public:
        GlyphSet(std::uint32_t first_codepoint, std::vector<GlyphCell> glyphs)
            : first_codepoint_(first_codepoint), glyphs_(std::move(glyphs))
        {
        }

        // Falls back to Missing() for any codepoint this set does not cover, so
        // callers never hold a nullable glyph.
        const GlyphCell &Find(std::uint32_t codepoint) const noexcept
        {
            if (codepoint < first_codepoint_)
            {
                return missing_;
            }
            const std::size_t index = codepoint - first_codepoint_;
            if (index >= glyphs_.size())
            {
                return missing_;
            }
            return glyphs_[index];
        }

        // Defaults to a blank fullwidth cell, which renders nothing. Replace it
        // with a visible tofu box when a missing glyph should be diagnosable.
        const GlyphCell &Missing() const noexcept { return missing_; }
        void SetMissing(GlyphCell missing) noexcept { missing_ = missing; }

        std::uint32_t FirstCodepoint() const noexcept { return first_codepoint_; }
        std::size_t Count() const noexcept { return glyphs_.size(); }

        // True when every halfwidth glyph has a clear right half.
        bool HasCleanHalfwidths() const noexcept
        {
            for (const GlyphCell &glyph : glyphs_)
            {
                if (!glyph.HasCleanHalfwidth())
                {
                    return false;
                }
            }
            return true;
        }

    private:
        std::uint32_t first_codepoint_ = 0u;
        std::vector<GlyphCell> glyphs_;
        GlyphCell missing_{};
    };
}

#endif
