#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "panel.h"
#include "panel_glyph_import.h"
#include "utf8.h"

namespace kpengine::panel
{
    namespace
    {
        // The ASCII range, baked repeatedly by several tests. The full product
        // range spans 0x20..0x9FFF, which is far too slow to rasterise per test.
        constexpr std::uint32_t kAsciiFirst = 0x20u;
        constexpr std::uint32_t kAsciiLast = 0x7Eu;

        // U+4E2D, a dense hanzi that fills its cell, and the surrounding block.
        constexpr std::uint32_t kCjkFirst = 0x4E00u;
        constexpr std::uint32_t kCjkLast = 0x4E30u;
        constexpr char kCjkMiddleUtf8[] = "\xE4\xB8\xAD";

        std::uint32_t PopCount(std::uint16_t value)
        {
            std::uint32_t count = 0u;
            while (value != 0u)
            {
                count += static_cast<std::uint32_t>(value & 1u);
                value = static_cast<std::uint16_t>(value >> 1u);
            }
            return count;
        }

        std::uint32_t CountInk(const GlyphCell &glyph)
        {
            std::uint32_t count = 0u;
            for (const std::uint16_t row : glyph.rows)
            {
                count += PopCount(row);
            }
            return count;
        }

        // Rows from the first inked row through the last, inclusive. A face
        // designs a hanzi to fill the em box and a Latin capital to sit inside
        // it, so this is what separates the two without hard-coding margins.
        std::uint32_t InkedRowSpan(const GlyphCell &glyph)
        {
            std::uint32_t first = kGlyphRows;
            std::uint32_t last = 0u;
            for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
            {
                if (glyph.rows[row] != 0u)
                {
                    if (first == kGlyphRows)
                    {
                        first = row;
                    }
                    last = row;
                }
            }
            return first == kGlyphRows ? 0u : (last - first + 1u);
        }

        // The column of the first row carrying exactly one dot. For 'E', 'L'
        // and 'J' that row is the stem, which is what makes this a mirror test:
        // a horizontally flipped rasteriser puts the stem on the wrong side
        // while leaving the glyph perfectly plausible-looking.
        std::uint32_t StemColumn(const GlyphCell &glyph)
        {
            for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
            {
                std::uint32_t inked = 0u;
                std::uint32_t position = 0u;
                for (std::uint32_t column = 0u; column < kGlyphColumns; ++column)
                {
                    if (glyph.TestDot(row, column))
                    {
                        ++inked;
                        position = column;
                    }
                }
                if (inked == 1u)
                {
                    return position;
                }
            }
            return kGlyphColumns;
        }

        bool RowHasInk(const GlyphCell &glyph, std::uint32_t row)
        {
            return glyph.rows[row] != 0u;
        }

        std::uint32_t CountMatrixInk(const DotMatrix &matrix)
        {
            std::uint32_t count = 0u;
            for (std::uint32_t row = 0u; row < matrix.Height(); ++row)
            {
                for (std::uint32_t column = 0u; column < matrix.Width(); ++column)
                {
                    if (matrix.TestDot(column, row))
                    {
                        ++count;
                    }
                }
            }
            return count;
        }

#if defined(KPENGINE_PANEL_FONT_PATH)
        GlyphBakeResult BakeRange(std::uint32_t first, std::uint32_t last)
        {
            GlyphBakeRequest request{};
            request.font_path = KPENGINE_PANEL_FONT_PATH;
            request.first_codepoint = first;
            request.last_codepoint = last;
            return BakeGlyphProduct(request);
        }

        GlyphCell BakeGlyph(std::uint32_t codepoint)
        {
            return BakeRange(codepoint, codepoint).product.glyphs.front();
        }
#endif
    }

    // The two rejection cases below need no font, so they always run.
    TEST(PanelGlyphImportTest, RejectsAReversedRange)
    {
        GlyphBakeRequest request{};
        request.font_path = "unused.ttf";
        request.first_codepoint = 0x40u;
        request.last_codepoint = 0x20u;
        request.pixel_height = kDefaultGlyphPixelHeight;

        EXPECT_THROW(BakeGlyphProduct(request), std::runtime_error);
    }

    TEST(PanelGlyphImportTest, RejectsAZeroPixelHeight)
    {
        GlyphBakeRequest request{};
        request.font_path = "unused.ttf";
        request.first_codepoint = 0x41u;
        request.last_codepoint = 0x41u;
        request.pixel_height = 0u;

        EXPECT_THROW(BakeGlyphProduct(request), std::runtime_error);
    }

    TEST(PanelGlyphImportTest, RejectsAnUnreadableFont)
    {
        GlyphBakeRequest request{};
        request.font_path = "no-such-font.ttf";
        request.first_codepoint = 0x41u;
        request.last_codepoint = 0x41u;

        EXPECT_THROW(BakeGlyphProduct(request), std::runtime_error);
    }

#if defined(KPENGINE_PANEL_FONT_PATH)
    TEST(PanelGlyphImportTest, LatinBakesHalfwidthAndCjkBakesFullwidth)
    {
        const GlyphBakeResult latin = BakeRange(0x41u, 0x5Au); // A..Z
        for (const GlyphCell &glyph : latin.product.glyphs)
        {
            ASSERT_GT(CountInk(glyph), 0u);
            EXPECT_EQ(glyph.advance, kHalfwidthAdvance);
        }

        const GlyphBakeResult cjk = BakeRange(kCjkFirst, kCjkLast);
        for (const GlyphCell &glyph : cjk.product.glyphs)
        {
            if (CountInk(glyph) == 0u)
            {
                continue; // unassigned in this face
            }
            EXPECT_EQ(glyph.advance, kFullwidthAdvance);
        }
    }

    TEST(PanelGlyphImportTest, EveryHalfwidthGlyphLeavesItsRightHalfClear)
    {
        // The halfwidth invariant is what stops a glyph bleeding into the
        // character after it, and the rasteriser can violate it.
        const GlyphBakeResult baked = BakeRange(kAsciiFirst, kAsciiLast);

        EXPECT_TRUE(ToGlyphSet(baked.product).HasCleanHalfwidths());
    }

    TEST(PanelGlyphImportTest, LatinStemsStayOnTheSideTheyWereDrawnOn)
    {
        // 'E' and 'L' carry their stem in the left half and 'J' in the right.
        // A mirrored glyph still reads as a plausible letter, so this is the
        // assertion that actually catches a flipped rasteriser.
        const GlyphCell e = BakeGlyph(0x45u);
        const GlyphCell l = BakeGlyph(0x4Cu);
        const GlyphCell j = BakeGlyph(0x4Au);

        EXPECT_LT(StemColumn(e), kHalfwidthAdvance / 2u);
        EXPECT_LT(StemColumn(l), kHalfwidthAdvance / 2u);
        EXPECT_GE(StemColumn(j), kHalfwidthAdvance / 2u);
    }

    TEST(PanelGlyphImportTest, CjkFillsMoreOfItsCellThanLatinDoes)
    {
        // Same face and same baseline, so this is self-calibrating: a hanzi is
        // designed to fill the em box while a Latin capital sits inside it.
        const GlyphCell middle = BakeGlyph(0x4E2Du);
        const GlyphCell capital = BakeGlyph(0x41u);

        ASSERT_GT(CountInk(middle), 0u);
        ASSERT_GT(CountInk(capital), 0u);
        EXPECT_GT(InkedRowSpan(middle), InkedRowSpan(capital));
        EXPECT_GT(CountInk(middle), CountInk(capital));
    }

    TEST(PanelGlyphImportTest, LatinWithoutADescenderStaysAboveTheBottomRow)
    {
        // 'A' has no descender, so its ink must stop at the baseline rather
        // than filling the cell the way CJK does. This is the vertical half of
        // the placement contract.
        const GlyphCell a = BakeGlyph(0x41u);

        ASSERT_GT(CountInk(a), 0u);
        EXPECT_FALSE(RowHasInk(a, kGlyphRows - 1u));
    }

    TEST(PanelGlyphImportTest, UnassignedCodepointsBakeBlankAndStillAdvance)
    {
        // U+E000 is a private-use codepoint that no normal face maps.
        const GlyphCell blank = BakeGlyph(0xE000u);

        EXPECT_EQ(CountInk(blank), 0u);
        EXPECT_EQ(blank.advance, kFullwidthAdvance);
    }

    TEST(PanelGlyphImportTest, MissingGlyphIsAVisibleBoxRatherThanBlank)
    {
        const GlyphBakeResult baked = BakeRange(0x41u, 0x41u);

        EXPECT_GT(CountInk(baked.product.missing), 0u);
        // A hollow box: corners are inked, the middle is not.
        EXPECT_TRUE(baked.product.missing.TestDot(0u, 0u));
        EXPECT_TRUE(baked.product.missing.TestDot(kGlyphRows - 1u, kGlyphColumns - 1u));
        EXPECT_FALSE(baked.product.missing.TestDot(kGlyphRows / 2u, kGlyphColumns / 2u));
    }

    TEST(PanelGlyphImportTest, RepeatedCharactersLandIdenticallyAndDistinctOnesDoNot)
    {
        const GlyphSet glyphs = ToGlyphSet(BakeRange(kAsciiFirst, kAsciiLast).product);

        Panel panel{4u, 1u};
        panel.SetText(0u, "OO");
        const DotMatrix repeated = panel.Rebuild(glyphs);

        for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
        {
            for (std::uint32_t column = 0u; column < kHalfwidthAdvance; ++column)
            {
                EXPECT_EQ(repeated.TestDot(column, row),
                          repeated.TestDot(column + kHalfwidthAdvance, row))
                    << "dot (" << column << ", " << row << ")";
            }
        }

        panel.SetText(0u, "Ov");
        EXPECT_NE(repeated, panel.Rebuild(glyphs));
    }

    TEST(PanelGlyphImportTest, ThreeHalfwidthCharactersStayWithinTwentyFourDots)
    {
        const GlyphSet glyphs = ToGlyphSet(BakeRange(kAsciiFirst, kAsciiLast).product);

        Panel panel{2u, 1u}; // 32 dots wide
        panel.SetText(0u, "OvO");
        const DotMatrix matrix = panel.Rebuild(glyphs);

        EXPECT_GT(CountMatrixInk(matrix), 0u);
        for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
        {
            for (std::uint32_t column = 24u; column < 32u; ++column)
            {
                EXPECT_FALSE(matrix.TestDot(column, row))
                    << "dot (" << column << ", " << row << ")";
            }
        }
    }

    TEST(PanelGlyphImportTest, CjkOccupiesAWholeSixteenDotCell)
    {
        const GlyphSet glyphs = ToGlyphSet(BakeRange(kCjkFirst, kCjkLast).product);

        // One column holds exactly one fullwidth character, so a second one
        // must clip rather than spilling into the next cell.
        Panel panel{1u, 1u};
        panel.SetText(0u, std::string{kCjkMiddleUtf8} + kCjkMiddleUtf8);
        const DotMatrix matrix = panel.Rebuild(glyphs);

        EXPECT_GT(CountMatrixInk(matrix), 0u);
        EXPECT_EQ(matrix.Width(), kGlyphColumns);
    }

    TEST(PanelGlyphImportTest, LosesNoInkAtTheDefaultSizeAndBaseline)
    {
        // The shipped configuration must fit the cell exactly. A non-zero
        // count here means the default bake is silently dropping ink.
        const GlyphBakeResult ascii = BakeRange(kAsciiFirst, kAsciiLast);
        EXPECT_EQ(ascii.clipped_glyphs, 0u);

        const GlyphBakeResult cjk = BakeRange(kCjkFirst, kCjkLast);
        EXPECT_EQ(cjk.clipped_glyphs, 0u);
    }

    TEST(PanelGlyphImportTest, ReportsInkClippedByTheCell)
    {
        // A baseline just inside the cell is legal but leaves no room for the
        // glyph, so nearly every one loses ink. Without the count this bake is
        // indistinguishable from a correct one.
        GlyphBakeRequest request{};
        request.font_path = KPENGINE_PANEL_FONT_PATH;
        request.first_codepoint = kAsciiFirst;
        request.last_codepoint = kAsciiLast;
        request.baseline_row = 1;

        const GlyphBakeResult clipped = BakeGlyphProduct(request);

        EXPECT_GT(clipped.clipped_glyphs, 0u);
    }

    TEST(PanelGlyphImportTest, RejectsABaselineOutsideTheCell)
    {
        // A face whose em does not map onto the cell would otherwise bake a
        // uniformly clipped set that still looks plausible in aggregate.
        GlyphBakeRequest request{};
        request.font_path = KPENGINE_PANEL_FONT_PATH;
        request.first_codepoint = 0x41u;
        request.last_codepoint = 0x41u;
        request.baseline_row = static_cast<std::int32_t>(request.pixel_height);

        EXPECT_THROW(BakeGlyphProduct(request), std::runtime_error);
    }

    TEST(PanelGlyphImportTest, RejectsANegativeBaseline)
    {
        GlyphBakeRequest request{};
        request.font_path = KPENGINE_PANEL_FONT_PATH;
        request.first_codepoint = 0x41u;
        request.last_codepoint = 0x41u;
        request.baseline_row = 0;

        EXPECT_THROW(BakeGlyphProduct(request), std::runtime_error);
    }

    TEST(PanelGlyphImportTest, BakesAProductThroughTheRealPipeline)
    {
        // The whole path a caller takes: bake, convert, lay out, render.
        const GlyphBakeResult baked = BakeRange(kAsciiFirst, kAsciiLast);

        ASSERT_EQ(baked.product.glyphs.size(), kAsciiLast - kAsciiFirst + 1u);
        EXPECT_GT(baked.baseline_row, 0);

        const GlyphSet glyphs = ToGlyphSet(baked.product);
        Panel panel{2u, 1u};
        panel.SetText(0u, "Hi");
        EXPECT_GT(CountMatrixInk(panel.Rebuild(glyphs)), 0u);
    }
#endif
}
