#include <gtest/gtest.h>

#include <cstdint>

#include "dot_matrix.h"
#include "test_glyphs.h"

namespace kpengine::panel
{
    namespace
    {
        // The left and right halves of a fullwidth cell, so an operation that
        // sets one and leaves the other is visible in the assertions.
        const GlyphCell &LeftBand()
        {
            static const GlyphCell glyph = test::MakeBandGlyph(0u, 8u, kFullwidthAdvance);
            return glyph;
        }

        const GlyphCell &RightBand()
        {
            static const GlyphCell glyph = test::MakeBandGlyph(8u, 8u, kFullwidthAdvance);
            return glyph;
        }

        bool ColumnIsSet(const DotMatrix &matrix, std::uint32_t column)
        {
            return matrix.TestDot(column, 0u);
        }
    }

    TEST(DotMatrixTest, PadsTheRowStrideToWholeWords)
    {
        EXPECT_EQ(RowStrideBytes(0u), 0u);
        EXPECT_EQ(RowStrideBytes(1u), 8u);
        EXPECT_EQ(RowStrideBytes(64u), 8u);
        EXPECT_EQ(RowStrideBytes(512u), 64u);
        // 500 dots need 63 bytes, which rounds up to the next whole word.
        EXPECT_EQ(RowStrideBytes(500u), 64u);
        EXPECT_EQ(RowStrideBytes(513u), 72u);
    }

    TEST(DotMatrixTest, TreatsOutOfRangeDotsAsOff)
    {
        const DotMatrix matrix(16u, 8u);

        EXPECT_FALSE(matrix.TestDot(16u, 0u));
        EXPECT_FALSE(matrix.TestDot(0u, 8u));
    }

    TEST(DotMatrixTest, ReplaceOverwritesTheDestination)
    {
        DotMatrix matrix(32u, 16u);
        matrix.StampGlyph(LeftBand(), 0u, 0u, MergeOp::Or);
        matrix.StampGlyph(RightBand(), 0u, 0u, MergeOp::Replace);

        EXPECT_FALSE(ColumnIsSet(matrix, 0u));
        EXPECT_FALSE(ColumnIsSet(matrix, 7u));
        EXPECT_TRUE(ColumnIsSet(matrix, 8u));
        EXPECT_TRUE(ColumnIsSet(matrix, 15u));
    }

    TEST(DotMatrixTest, OrUnionsTheSourceIntoTheDestination)
    {
        DotMatrix matrix(32u, 16u);
        matrix.StampGlyph(LeftBand(), 0u, 0u, MergeOp::Or);
        matrix.StampGlyph(RightBand(), 0u, 0u, MergeOp::Or);

        EXPECT_TRUE(ColumnIsSet(matrix, 0u));
        EXPECT_TRUE(ColumnIsSet(matrix, 7u));
        EXPECT_TRUE(ColumnIsSet(matrix, 8u));
        EXPECT_TRUE(ColumnIsSet(matrix, 15u));
    }

    TEST(DotMatrixTest, XorClearsBitsTheSourceAlsoSets)
    {
        DotMatrix matrix(32u, 16u);
        matrix.StampGlyph(LeftBand(), 0u, 0u, MergeOp::Or);
        matrix.StampGlyph(RightBand(), 0u, 0u, MergeOp::Or);
        matrix.StampGlyph(LeftBand(), 0u, 0u, MergeOp::Xor);

        EXPECT_FALSE(ColumnIsSet(matrix, 0u));
        EXPECT_FALSE(ColumnIsSet(matrix, 7u));
        EXPECT_TRUE(ColumnIsSet(matrix, 8u));
        EXPECT_TRUE(ColumnIsSet(matrix, 15u));
    }

    TEST(DotMatrixTest, AndNotErasesTheStampedShape)
    {
        DotMatrix matrix(32u, 16u);
        matrix.StampGlyph(LeftBand(), 0u, 0u, MergeOp::Or);
        matrix.StampGlyph(RightBand(), 0u, 0u, MergeOp::Or);
        matrix.StampGlyph(RightBand(), 0u, 0u, MergeOp::AndNot);

        EXPECT_TRUE(ColumnIsSet(matrix, 0u));
        EXPECT_TRUE(ColumnIsSet(matrix, 7u));
        EXPECT_FALSE(ColumnIsSet(matrix, 8u));
        EXPECT_FALSE(ColumnIsSet(matrix, 15u));
    }

    TEST(DotMatrixTest, IgnoresAMisalignedStampInsteadOfShiftingIt)
    {
        DotMatrix matrix(32u, 16u);
        matrix.StampGlyph(LeftBand(), 4u, 0u, MergeOp::Or);

        for (std::uint32_t column = 0u; column < 32u; ++column)
        {
            EXPECT_FALSE(ColumnIsSet(matrix, column)) << "column " << column;
        }
    }

    TEST(DotMatrixTest, ClipsAtTheRightEdgeWithoutSpillingIntoRowPadding)
    {
        // 16 dots of width pad to an 8-byte stride, so bytes 2..7 are padding.
        DotMatrix matrix(16u, 16u);
        matrix.StampGlyph(test::FullwidthGlyph(), 8u, 0u, MergeOp::Replace);

        EXPECT_TRUE(ColumnIsSet(matrix, 8u));
        EXPECT_TRUE(ColumnIsSet(matrix, 15u));
        for (std::uint32_t byte = 2u; byte < matrix.StrideBytes(); ++byte)
        {
            EXPECT_EQ(matrix.Bytes()[byte], 0u) << "padding byte " << byte;
        }
    }

    TEST(DotMatrixTest, ClipsAtTheBottomEdge)
    {
        DotMatrix matrix(16u, 8u);
        matrix.StampGlyph(test::FullwidthGlyph(), 0u, 4u, MergeOp::Replace);

        // Glyph rows 0..3 land on matrix rows 4..7; the rest are dropped.
        EXPECT_TRUE(matrix.TestDot(0u, 4u));
        EXPECT_TRUE(matrix.TestDot(0u, 7u));
        EXPECT_FALSE(matrix.TestDot(0u, 8u));
        EXPECT_EQ(matrix.Bytes().size(), matrix.StrideBytes() * 8u);
    }

    TEST(DotMatrixTest, ClearReturnsEveryDotToOff)
    {
        DotMatrix matrix(32u, 16u);
        matrix.StampGlyph(test::FullwidthGlyph(), 0u, 0u, MergeOp::Replace);
        matrix.Clear();

        for (std::uint32_t column = 0u; column < 32u; ++column)
        {
            EXPECT_FALSE(ColumnIsSet(matrix, column)) << "column " << column;
        }
    }

    TEST(DotMatrixTest, ComparesByExtentAndBits)
    {
        DotMatrix first(32u, 16u);
        DotMatrix second(32u, 16u);
        EXPECT_EQ(first, second);

        second.StampGlyph(test::FullwidthGlyph(), 0u, 0u, MergeOp::Replace);
        EXPECT_NE(first, second);

        first.StampGlyph(test::FullwidthGlyph(), 0u, 0u, MergeOp::Replace);
        EXPECT_EQ(first, second);

        const DotMatrix narrower(16u, 16u);
        EXPECT_NE(first, narrower);
    }
}
