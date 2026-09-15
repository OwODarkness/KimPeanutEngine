#include <gtest/gtest.h>

#include <vector>

#include "glyph_cell.h"
#include "test_glyphs.h"

namespace kpengine::panel
{
    TEST(GlyphCellTest, AddressesDotsByColumnAndRow)
    {
        GlyphCell glyph;
        glyph.SetDot(3u, 11u, true);

        EXPECT_TRUE(glyph.TestDot(3u, 11u));
        EXPECT_FALSE(glyph.TestDot(3u, 10u));
        EXPECT_FALSE(glyph.TestDot(4u, 11u));

        glyph.SetDot(3u, 11u, false);
        EXPECT_FALSE(glyph.TestDot(3u, 11u));
    }

    TEST(GlyphCellTest, RejectsOutOfRangeDotAccess)
    {
        GlyphCell glyph;
        glyph.SetDot(kGlyphRows, 0u, true);
        glyph.SetDot(0u, kGlyphColumns, true);

        EXPECT_FALSE(glyph.TestDot(kGlyphRows, 0u));
        EXPECT_FALSE(glyph.TestDot(0u, kGlyphColumns));
        EXPECT_EQ(glyph.rows[0], 0u);
    }

    TEST(GlyphCellTest, TreatsAnAdvanceBelowTheStoredWidthAsHalfwidth)
    {
        const GlyphCell halfwidth = test::HalfwidthGlyph();
        const GlyphCell fullwidth = test::FullwidthGlyph();

        EXPECT_TRUE(halfwidth.IsHalfwidth());
        EXPECT_FALSE(fullwidth.IsHalfwidth());
    }

    TEST(GlyphCellTest, AcceptsAHalfwidthGlyphThatLeavesItsRightHalfClear)
    {
        EXPECT_TRUE(test::HalfwidthGlyph().HasCleanHalfwidth());
    }

    TEST(GlyphCellTest, RejectsAHalfwidthGlyphThatBleedsIntoTheNextCharacter)
    {
        // A glyph that advances 8 but draws into columns 8..15 would overwrite
        // the character after it.
        const GlyphCell bleeding = test::MakeBandGlyph(8u, 8u, kHalfwidthAdvance);

        EXPECT_FALSE(bleeding.HasCleanHalfwidth());
    }

    TEST(GlyphCellTest, AllowsAFullwidthGlyphToUseEveryColumn)
    {
        EXPECT_TRUE(test::FullwidthGlyph().HasCleanHalfwidth());
    }

    TEST(GlyphSetTest, FindsCodepointsInsideItsRange)
    {
        const GlyphSet glyphs = test::MakeHalfwidthSet(0x4E00u, 4u);

        EXPECT_EQ(glyphs.FirstCodepoint(), 0x4E00u);
        EXPECT_EQ(glyphs.Count(), 4u);
        EXPECT_TRUE(glyphs.Find(0x4E02u).TestDot(0u, 0u));
    }

    TEST(GlyphSetTest, FallsBackToTheMissingGlyphOutsideItsRange)
    {
        const GlyphSet glyphs = test::MakeHalfwidthSet(0x4E00u, 2u);

        const GlyphCell &below = glyphs.Find(0x4DFFu);
        const GlyphCell &above = glyphs.Find(0x4E02u);
        EXPECT_EQ(&below, &glyphs.Missing());
        EXPECT_EQ(&above, &glyphs.Missing());
    }

    TEST(GlyphSetTest, UsesAConfiguredMissingGlyph)
    {
        GlyphSet glyphs = test::MakeHalfwidthSet(0x4E00u, 1u);
        const GlyphCell tofu = test::FullwidthGlyph();
        glyphs.SetMissing(tofu);

        EXPECT_TRUE(glyphs.Find(0x9999u).TestDot(0u, 0u));
        EXPECT_EQ(glyphs.Find(0x9999u).advance, kFullwidthAdvance);
    }

    TEST(GlyphSetTest, ReportsASetContainingABleedingHalfwidthGlyph)
    {
        const GlyphSet clean = test::MakeHalfwidthSet(0x41u, 3u);
        EXPECT_TRUE(clean.HasCleanHalfwidths());

        const GlyphSet bleeding(
            0x41u, std::vector<GlyphCell>{test::HalfwidthGlyph(),
                                          test::MakeBandGlyph(8u, 8u, kHalfwidthAdvance)});
        EXPECT_FALSE(bleeding.HasCleanHalfwidths());
    }
}
