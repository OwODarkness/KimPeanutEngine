#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "panel.h"
#include "test_glyphs.h"

namespace kpengine::panel
{
    namespace
    {
        // Two adjacent codepoints so a two-glyph set covers both. These tests
        // are about placement, not about which characters the codepoints name.
        constexpr char32_t kFirstCodepoint = 0x4F60u;
        constexpr std::string_view kFirstUtf8 = "\xE4\xBD\xA0";
        constexpr std::string_view kSecondUtf8 = "\xE4\xBD\xA1";
        constexpr std::string_view kFirstAndSecondUtf8 = "\xE4\xBD\xA0\xE4\xBD\xA1";
        constexpr std::string_view kFirstSecondFirstUtf8 =
            "\xE4\xBD\xA0\xE4\xBD\xA1\xE4\xBD\xA0";
        // ASCII 'A' followed by the second codepoint above.
        constexpr std::string_view kUncoveredThenSecondUtf8 = "A\xE4\xBD\xA1";

        GlyphSet TwoFullwidthGlyphs()
        {
            return test::MakeFullwidthSet(kFirstCodepoint, 2u);
        }

        GlyphSet ThreeHalfwidthGlyphs()
        {
            return test::MakeHalfwidthSet(static_cast<std::uint32_t>('A'), 3u);
        }

        void ExpectRegionSet(const DotMatrix &matrix, std::uint32_t first_column,
                             std::uint32_t last_column, std::uint32_t first_row,
                             std::uint32_t last_row)
        {
            for (std::uint32_t row = first_row; row <= last_row; ++row)
            {
                for (std::uint32_t column = first_column; column <= last_column;
                     ++column)
                {
                    EXPECT_TRUE(matrix.TestDot(column, row))
                        << "dot (" << column << ", " << row << ")";
                }
            }
        }

        void ExpectRegionClear(const DotMatrix &matrix, std::uint32_t first_column,
                               std::uint32_t last_column, std::uint32_t first_row,
                               std::uint32_t last_row)
        {
            for (std::uint32_t row = first_row; row <= last_row; ++row)
            {
                for (std::uint32_t column = first_column; column <= last_column;
                     ++column)
                {
                    EXPECT_FALSE(matrix.TestDot(column, row))
                        << "dot (" << column << ", " << row << ")";
                }
            }
        }

        void ExpectRowsClear(const DotMatrix &matrix, std::uint32_t first_row,
                             std::uint32_t last_row)
        {
            ExpectRegionClear(matrix, 0u, matrix.Width() - 1u, first_row, last_row);
        }
    }

    TEST(PanelTest, ReportsItsExtentInDots)
    {
        const Panel panel(4u, 2u);

        EXPECT_EQ(panel.Columns(), 4u);
        EXPECT_EQ(panel.Rows(), 2u);
        EXPECT_EQ(PanelDotWidth(4u), 64u);
        EXPECT_EQ(PanelDotHeight(2u), 32u);
    }

    TEST(PanelTest, RebuildingUnchangedContentIsByteIdentical)
    {
        Panel panel(4u, 2u);
        panel.SetText(0u, kFirstAndSecondUtf8);
        panel.SetText(1u, "ABC");
        const GlyphSet glyphs = TwoFullwidthGlyphs();

        const DotMatrix first = panel.Rebuild(glyphs);
        const DotMatrix second = panel.Rebuild(glyphs);

        EXPECT_EQ(first, second);
    }

    TEST(PanelTest, RebuildingClearsDotsLeftByALongerLine)
    {
        Panel panel(2u, 1u);
        const GlyphSet glyphs = TwoFullwidthGlyphs();

        // Two fullwidth characters fill all 32 dots of the row.
        panel.SetText(0u, kFirstAndSecondUtf8);
        const DotMatrix wide = panel.Rebuild(glyphs);
        ExpectRegionSet(wide, 0u, 31u, 0u, kGlyphRows - 1u);

        // The second character is gone, so its dots must be gone too. This is
        // the failure the logical layer exists to prevent.
        panel.SetText(0u, kFirstUtf8);
        const DotMatrix narrow = panel.Rebuild(glyphs);

        ExpectRegionSet(narrow, 0u, 15u, 0u, kGlyphRows - 1u);
        ExpectRegionClear(narrow, 16u, 31u, 0u, kGlyphRows - 1u);
    }

    TEST(PanelTest, HalfwidthCharactersAdvanceByEightDots)
    {
        Panel panel(4u, 1u);
        panel.SetText(0u, "ABC");

        const DotMatrix matrix = panel.Rebuild(ThreeHalfwidthGlyphs());

        // Three halfwidth characters occupy 24 dots. Advancing by the stored
        // width instead would place them at 0, 16 and 32.
        EXPECT_TRUE(matrix.TestDot(0u, 0u));
        EXPECT_TRUE(matrix.TestDot(7u, 0u));
        EXPECT_TRUE(matrix.TestDot(8u, 0u));
        EXPECT_TRUE(matrix.TestDot(15u, 0u));
        EXPECT_TRUE(matrix.TestDot(16u, 0u));
        EXPECT_TRUE(matrix.TestDot(23u, 0u));
        EXPECT_FALSE(matrix.TestDot(24u, 0u));
        EXPECT_FALSE(matrix.TestDot(31u, 0u));
    }

    TEST(PanelTest, ClipsTextWiderThanThePanelInsteadOfWrapping)
    {
        Panel panel(2u, 2u);
        panel.SetText(0u, kFirstSecondFirstUtf8);

        const DotMatrix matrix = panel.Rebuild(TwoFullwidthGlyphs());

        // The third character does not fit, so it is dropped rather than
        // spilling onto the row below.
        ExpectRegionSet(matrix, 0u, 31u, 0u, kGlyphRows - 1u);
        ExpectRowsClear(matrix, kGlyphRows, PanelDotHeight(2u) - 1u);
    }

    TEST(PanelTest, AdvanceOfAnUncoveredCodepointStillMovesTheCursor)
    {
        Panel panel(2u, 1u);
        const GlyphSet glyphs = TwoFullwidthGlyphs();

        // 'A' is outside the set, so it draws its blank missing glyph but still
        // advances a full width, leaving the covered character at dot 16.
        panel.SetText(0u, kUncoveredThenSecondUtf8);
        const DotMatrix matrix = panel.Rebuild(glyphs);

        ExpectRegionClear(matrix, 0u, 15u, 0u, kGlyphRows - 1u);
        ExpectRegionSet(matrix, 16u, 31u, 0u, kGlyphRows - 1u);
    }

    TEST(PanelTest, RowsAreIndependent)
    {
        Panel panel(2u, 2u);
        panel.SetText(0u, kFirstUtf8);

        const DotMatrix matrix = panel.Rebuild(TwoFullwidthGlyphs());

        ExpectRegionSet(matrix, 0u, 15u, 0u, kGlyphRows - 1u);
        ExpectRowsClear(matrix, kGlyphRows, PanelDotHeight(2u) - 1u);
    }

    TEST(PanelTest, CodepointsAreAvailableAsDecodedContent)
    {
        Panel panel(2u, 1u);
        panel.SetText(0u, kFirstAndSecondUtf8);

        const std::u32string &codepoints = panel.CodepointsAt(0u);
        ASSERT_EQ(codepoints.size(), 2u);
        EXPECT_EQ(codepoints[0], kFirstCodepoint);
        EXPECT_EQ(codepoints[1], kFirstCodepoint + 1u);
    }

    TEST(PanelTest, IgnoresARowOutsideThePanel)
    {
        Panel panel(2u, 1u);
        panel.SetText(1u, kFirstUtf8);

        EXPECT_TRUE(panel.CodepointsAt(1u).empty());
        ExpectRowsClear(panel.Rebuild(TwoFullwidthGlyphs()), 0u, kGlyphRows - 1u);
    }

    TEST(PanelTest, ClearRemovesEveryRowsContent)
    {
        Panel panel(2u, 2u);
        panel.SetText(0u, kFirstUtf8);
        panel.SetText(1u, kSecondUtf8);
        panel.Clear();

        const DotMatrix matrix = panel.Rebuild(TwoFullwidthGlyphs());

        EXPECT_TRUE(panel.CodepointsAt(0u).empty());
        ExpectRowsClear(matrix, 0u, PanelDotHeight(2u) - 1u);
    }

    TEST(PanelTest, SetTextAtPlacesTheRowAtTheGivenColumn)
    {
        Panel panel(4u, 1u); // 64 dots, so column 3 starts at dot 24
        panel.SetTextAt(0u, 3u, kFirstUtf8);

        ASSERT_EQ(panel.OriginAt(0u), 3u);
        const DotMatrix matrix = panel.Rebuild(TwoFullwidthGlyphs());

        ExpectRegionClear(matrix, 0u, 23u, 0u, kGlyphRows - 1u);
        ExpectRegionSet(matrix, 24u, 39u, 0u, kGlyphRows - 1u);
        ExpectRegionClear(matrix, 40u, 63u, 0u, kGlyphRows - 1u);
    }

    TEST(PanelTest, ColumnZeroIsTheSameAsTheUnalignedSetText)
    {
        Panel at_origin(2u, 1u);
        at_origin.SetTextAt(0u, 0u, kFirstAndSecondUtf8);

        Panel plain(2u, 1u);
        plain.SetText(0u, kFirstAndSecondUtf8);

        EXPECT_EQ(at_origin.Rebuild(TwoFullwidthGlyphs()),
                  plain.Rebuild(TwoFullwidthGlyphs()));
        EXPECT_EQ(at_origin.OriginAt(0u), 0u);
    }

    TEST(PanelTest, AnOriginNearTheRightEdgeClipsInsteadOfWrapping)
    {
        Panel panel(2u, 2u);
        panel.SetTextAt(0u, 1u, kFirstAndSecondUtf8);

        const DotMatrix matrix = panel.Rebuild(TwoFullwidthGlyphs());

        // The first character fits at dot 8; the second would end at dot 40.
        ExpectRegionClear(matrix, 0u, 7u, 0u, kGlyphRows - 1u);
        ExpectRegionSet(matrix, 8u, 23u, 0u, kGlyphRows - 1u);
        ExpectRegionClear(matrix, 24u, 31u, 0u, kGlyphRows - 1u);
        // Clipped, not wrapped onto the row below.
        ExpectRowsClear(matrix, kGlyphRows, PanelDotHeight(2u) - 1u);
    }

    TEST(PanelTest, AnOriginPastTheRightEdgeDrawsNothing)
    {
        const GlyphSet glyphs = TwoFullwidthGlyphs();

        // A 32-dot row holds four halfwidth steps, so column 4 is the first
        // that cannot fit a character and column 5 is past the edge entirely.
        Panel at_edge(2u, 1u);
        at_edge.SetTextAt(0u, 4u, kFirstUtf8);
        ExpectRowsClear(at_edge.Rebuild(glyphs), 0u, kGlyphRows - 1u);

        Panel past_edge(2u, 1u);
        past_edge.SetTextAt(0u, 5u, kFirstUtf8);
        ExpectRowsClear(past_edge.Rebuild(glyphs), 0u, kGlyphRows - 1u);
    }

    TEST(PanelTest, AnAbsurdOriginCannotOverflowTheCursor)
    {
        // The origin is compared against the panel width before it is scaled,
        // so the largest representable value is rejected rather than wrapped.
        Panel panel(2u, 1u);
        panel.SetTextAt(0u, 0xFFFFFFFFu, kFirstUtf8);

        ExpectRowsClear(panel.Rebuild(TwoFullwidthGlyphs()), 0u, kGlyphRows - 1u);
        EXPECT_EQ(panel.OriginAt(0u), 0xFFFFFFFFu);
    }

    TEST(PanelTest, OriginIsReportedAndResetByClear)
    {
        Panel panel(2u, 1u);
        EXPECT_EQ(panel.OriginAt(0u), 0u);

        panel.SetTextAt(0u, 2u, kFirstUtf8);
        EXPECT_EQ(panel.OriginAt(0u), 2u);
        EXPECT_EQ(panel.OriginAt(7u), 0u); // outside the panel

        // Clearing the panel empties the content, so a stale origin would be
        // the only state left behind.
        panel.Clear();
        EXPECT_EQ(panel.OriginAt(0u), 0u);
    }

    TEST(PanelTest, AnOriginCentresThreeHalfwidthCharacters)
    {
        // 64 dots with 24 dots of text leaves 40, so the centred origin is
        // 20 dots -- which rounds to 2 whole halfwidth steps.
        Panel panel(4u, 1u);
        panel.SetTextAt(0u, 2u, "ABC");

        const DotMatrix matrix = panel.Rebuild(ThreeHalfwidthGlyphs());

        ExpectRegionClear(matrix, 0u, 15u, 0u, kGlyphRows - 1u);
        ExpectRegionSet(matrix, 16u, 39u, 0u, kGlyphRows - 1u);
        ExpectRegionClear(matrix, 40u, 63u, 0u, kGlyphRows - 1u);
    }
}
