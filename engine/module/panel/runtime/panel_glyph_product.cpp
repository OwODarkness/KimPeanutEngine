#include "panel_glyph_product.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace kpengine::panel
{
    namespace
    {
        // Rows then advance, little-endian, with no padding.
        constexpr std::size_t kGlyphBytes = (kGlyphRows * 2u) + 4u;

        void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
        {
            for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
            }
        }

        void AppendGlyph(std::vector<std::uint8_t> &bytes, const GlyphCell &glyph)
        {
            for (const std::uint16_t row : glyph.rows)
            {
                bytes.push_back(static_cast<std::uint8_t>(row & 0xFFu));
                bytes.push_back(static_cast<std::uint8_t>((row >> 8u) & 0xFFu));
            }
            AppendU32(bytes, glyph.advance);
        }

        class Reader final
        {
        public:
            Reader(const std::vector<std::uint8_t> &bytes, const std::string &origin)
                : bytes_(bytes), origin_(origin)
            {
            }

            std::uint8_t ReadByte()
            {
                Require(1u);
                return bytes_[cursor_++];
            }

            std::uint32_t ReadU32()
            {
                std::uint32_t value = 0u;
                for (std::uint32_t index = 0u; index < 4u; ++index)
                {
                    // Cast before shifting: a byte above 0x7F shifted into the
                    // top octet would overflow a signed int.
                    value |= static_cast<std::uint32_t>(ReadByte()) << (index * 8u);
                }
                return value;
            }

            GlyphCell ReadGlyph()
            {
                Require(kGlyphRows * 2u);
                GlyphCell glyph;
                for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
                {
                    const std::size_t offset = cursor_ + (row * 2u);
                    glyph.rows[row] = static_cast<std::uint16_t>(
                        bytes_[offset] | (bytes_[offset + 1u] << 8u));
                }
                cursor_ += kGlyphRows * 2u;
                glyph.advance = ReadU32();
                return glyph;
            }

            bool AtEnd() const noexcept { return cursor_ == bytes_.size(); }

        private:
            void Require(std::size_t count) const
            {
                if (cursor_ + count > bytes_.size())
                {
                    throw std::runtime_error(origin_ + ": truncated body");
                }
            }

            const std::vector<std::uint8_t> &bytes_;
            const std::string &origin_;
            std::size_t cursor_ = 0u;
        };
    }

    GlyphSet ToGlyphSet(PanelGlyphProduct product)
    {
        GlyphSet glyphs(product.first_codepoint, std::move(product.glyphs));
        glyphs.SetMissing(product.missing);
        return glyphs;
    }

    void WritePanelGlyphProduct(const PanelGlyphProduct &product,
                                const std::filesystem::path &path)
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(kPanelGlyphProductMagic.size() + 16u +
                      (product.glyphs.size() * kGlyphBytes));

        for (const char character : kPanelGlyphProductMagic)
        {
            bytes.push_back(static_cast<std::uint8_t>(character));
        }
        AppendU32(bytes, kPanelGlyphProductVersion);
        AppendU32(bytes, product.first_codepoint);
        AppendU32(bytes, static_cast<std::uint32_t>(product.glyphs.size()));
        AppendGlyph(bytes, product.missing);
        for (const GlyphCell &glyph : product.glyphs)
        {
            AppendGlyph(bytes, glyph);
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            throw std::runtime_error("panel glyph product: cannot write '" +
                                     path.string() + "'");
        }
        stream.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream)
        {
            throw std::runtime_error("panel glyph product: failed writing '" +
                                     path.string() + "'");
        }
    }

    PanelGlyphProduct ReadPanelGlyphProduct(const std::filesystem::path &path)
    {
        const std::string origin = "panel glyph product '" + path.string() + "'";

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error(origin + ": cannot open");
        }
        const std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};

        Reader reader(bytes, origin);
        for (const char expected : kPanelGlyphProductMagic)
        {
            if (static_cast<char>(reader.ReadByte()) != expected)
            {
                throw std::runtime_error(origin + ": not a panel glyph product");
            }
        }

        const std::uint32_t version = reader.ReadU32();
        if (version != kPanelGlyphProductVersion)
        {
            throw std::runtime_error(origin + ": unsupported version " +
                                     std::to_string(version));
        }

        PanelGlyphProduct product;
        product.first_codepoint = reader.ReadU32();
        const std::uint32_t count = reader.ReadU32();
        product.missing = reader.ReadGlyph();
        product.glyphs.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            product.glyphs.push_back(reader.ReadGlyph());
        }

        if (!reader.AtEnd())
        {
            throw std::runtime_error(origin + ": trailing data");
        }
        return product;
    }
}
