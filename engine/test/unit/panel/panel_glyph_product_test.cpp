#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include "panel_glyph_product.h"
#include "test_glyphs.h"

namespace kpengine::panel
{
    namespace
    {
        std::filesystem::path TempProductPath()
        {
            return std::filesystem::temp_directory_path() /
                   "kp_panel_glyph_product_test.kppnlgl";
        }

        void WriteRawBytes(const std::filesystem::path &path,
                           const std::vector<std::uint8_t> &bytes)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }

        std::vector<std::uint8_t> ReadRawBytes(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary);
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(stream),
                                             std::istreambuf_iterator<char>()};
        }

        PanelGlyphProduct MakeProduct()
        {
            PanelGlyphProduct product;
            product.first_codepoint = 0x4F60u;
            product.glyphs = {test::FullwidthGlyph(), test::HalfwidthGlyph(),
                              test::MakeBandGlyph(8u, 8u, kFullwidthAdvance)};
            product.missing = test::MakeBandGlyph(2u, 4u, kFullwidthAdvance);
            return product;
        }
    }

    TEST(PanelGlyphProductTest, RoundTripsEveryField)
    {
        const PanelGlyphProduct product = MakeProduct();
        const std::filesystem::path path = TempProductPath();

        WritePanelGlyphProduct(product, path);
        const PanelGlyphProduct loaded = ReadPanelGlyphProduct(path);
        std::filesystem::remove(path);

        EXPECT_EQ(loaded.first_codepoint, product.first_codepoint);
        ASSERT_EQ(loaded.glyphs.size(), product.glyphs.size());
        for (std::size_t index = 0u; index < product.glyphs.size(); ++index)
        {
            EXPECT_EQ(loaded.glyphs[index].rows, product.glyphs[index].rows)
                << "glyph " << index;
            EXPECT_EQ(loaded.glyphs[index].advance, product.glyphs[index].advance)
                << "glyph " << index;
        }
        EXPECT_EQ(loaded.missing.rows, product.missing.rows);
        EXPECT_EQ(loaded.missing.advance, product.missing.advance);
    }

    TEST(PanelGlyphProductTest, RoundTripsAnEmptyRange)
    {
        PanelGlyphProduct product;
        product.first_codepoint = 0x41u;
        const std::filesystem::path path = TempProductPath();

        WritePanelGlyphProduct(product, path);
        const PanelGlyphProduct loaded = ReadPanelGlyphProduct(path);
        std::filesystem::remove(path);

        EXPECT_TRUE(loaded.glyphs.empty());
        EXPECT_EQ(loaded.first_codepoint, 0x41u);
    }

    TEST(PanelGlyphProductTest, BuildsAGlyphSetThatKeepsTheMissingGlyph)
    {
        PanelGlyphProduct product = MakeProduct();
        const GlyphCell expected_missing = product.missing;

        const GlyphSet glyphs = ToGlyphSet(std::move(product));

        EXPECT_EQ(glyphs.FirstCodepoint(), 0x4F60u);
        EXPECT_EQ(glyphs.Count(), 3u);
        EXPECT_EQ(glyphs.Find(0x4F63u).rows, expected_missing.rows);
        EXPECT_EQ(glyphs.Find(0x4F60u).advance, kFullwidthAdvance);
    }

    TEST(PanelGlyphProductTest, RejectsAFileWithAForeignMagic)
    {
        const std::filesystem::path path = TempProductPath();
        WriteRawBytes(path, {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u});

        EXPECT_THROW(ReadPanelGlyphProduct(path), std::runtime_error);
        std::filesystem::remove(path);
    }

    TEST(PanelGlyphProductTest, RejectsAnEmptyFile)
    {
        const std::filesystem::path path = TempProductPath();
        WriteRawBytes(path, {});

        EXPECT_THROW(ReadPanelGlyphProduct(path), std::runtime_error);
        std::filesystem::remove(path);
    }

    TEST(PanelGlyphProductTest, RejectsATruncatedBody)
    {
        const std::filesystem::path full = TempProductPath();
        WritePanelGlyphProduct(MakeProduct(), full);
        std::vector<std::uint8_t> bytes = ReadRawBytes(full);
        std::filesystem::remove(full);

        ASSERT_GT(bytes.size(), 8u);
        bytes.resize(bytes.size() - 8u);

        const std::filesystem::path truncated = TempProductPath();
        WriteRawBytes(truncated, bytes);
        EXPECT_THROW(ReadPanelGlyphProduct(truncated), std::runtime_error);
        std::filesystem::remove(truncated);
    }

    TEST(PanelGlyphProductTest, RejectsAnUnknownVersion)
    {
        const std::filesystem::path source = TempProductPath();
        WritePanelGlyphProduct(MakeProduct(), source);
        std::vector<std::uint8_t> bytes = ReadRawBytes(source);
        std::filesystem::remove(source);

        ASSERT_GT(bytes.size(), 12u);
        bytes[8] = 99u; // version is the four bytes after the magic
        bytes[9] = 0u;
        bytes[10] = 0u;
        bytes[11] = 0u;

        const std::filesystem::path patched = TempProductPath();
        WriteRawBytes(patched, bytes);
        EXPECT_THROW(ReadPanelGlyphProduct(patched), std::runtime_error);
        std::filesystem::remove(patched);
    }

    TEST(PanelGlyphProductTest, RejectsTrailingData)
    {
        const std::filesystem::path source = TempProductPath();
        WritePanelGlyphProduct(MakeProduct(), source);
        std::vector<std::uint8_t> bytes = ReadRawBytes(source);
        std::filesystem::remove(source);

        bytes.push_back(0u);

        const std::filesystem::path extended = TempProductPath();
        WriteRawBytes(extended, bytes);
        EXPECT_THROW(ReadPanelGlyphProduct(extended), std::runtime_error);
        std::filesystem::remove(extended);
    }

    TEST(PanelGlyphProductTest, RejectsAnUnreadablePath)
    {
        EXPECT_THROW(
            ReadPanelGlyphProduct(std::filesystem::path{"no-such-directory/nope"}),
            std::runtime_error);
    }
}
