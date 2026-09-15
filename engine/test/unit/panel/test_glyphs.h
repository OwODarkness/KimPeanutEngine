#ifndef KPENGINE_TEST_UNIT_PANEL_TEST_GLYPHS_H
#define KPENGINE_TEST_UNIT_PANEL_TEST_GLYPHS_H

#include <cstdint>
#include <vector>

#include "glyph_cell.h"

namespace kpengine::panel::test
{
    // A glyph whose columns [first_column, first_column + width) are solid
    // across every row. Band glyphs make the expected dot pattern of a stamp
    // something a test can state directly instead of deriving.
    inline GlyphCell MakeBandGlyph(std::uint32_t first_column,
                                   std::uint32_t width,
                                   std::uint32_t advance)
    {
        GlyphCell glyph;
        glyph.advance = advance;
        for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
        {
            for (std::uint32_t column = first_column; column < first_column + width;
                 ++column)
            {
                glyph.SetDot(row, column, true);
            }
        }
        return glyph;
    }

    // The left 8 columns, advancing 8: a well-formed halfwidth glyph.
    inline GlyphCell HalfwidthGlyph()
    {
        return MakeBandGlyph(0u, kHalfwidthAdvance, kHalfwidthAdvance);
    }

    // All 16 columns, advancing 16.
    inline GlyphCell FullwidthGlyph()
    {
        return MakeBandGlyph(0u, kGlyphColumns, kFullwidthAdvance);
    }

    inline GlyphSet MakeHalfwidthSet(std::uint32_t first_codepoint,
                                     std::size_t count)
    {
        return GlyphSet(first_codepoint,
                        std::vector<GlyphCell>(count, HalfwidthGlyph()));
    }

    inline GlyphSet MakeFullwidthSet(std::uint32_t first_codepoint,
                                     std::size_t count)
    {
        return GlyphSet(first_codepoint,
                        std::vector<GlyphCell>(count, FullwidthGlyph()));
    }
}

#endif
