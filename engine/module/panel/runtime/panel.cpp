#include "panel.h"

#include "utf8.h"

namespace kpengine::panel
{
    Panel::Panel(std::uint32_t columns, std::uint32_t rows)
        : columns_(columns), rows_(rows), lines_(rows)
    {
    }

    void Panel::Clear() noexcept
    {
        for (std::u32string &line : lines_)
        {
            line.clear();
        }
    }

    void Panel::SetText(std::uint32_t row, std::string_view utf8)
    {
        if (row >= rows_)
        {
            return;
        }
        lines_[row] = DecodeUtf8(utf8);
    }

    const std::u32string &Panel::CodepointsAt(std::uint32_t row) const noexcept
    {
        static const std::u32string kEmpty;
        if (row >= rows_)
        {
            return kEmpty;
        }
        return lines_[row];
    }

    DotMatrix Panel::Rebuild(const GlyphSet &glyphs) const
    {
        DotMatrix matrix(PanelDotWidth(columns_), PanelDotHeight(rows_));

        for (std::uint32_t row = 0u; row < rows_; ++row)
        {
            // Clipped against the glyph's advance rather than its stored width,
            // so a halfwidth glyph that fits by advance is still drawn even
            // though its 16 columns of storage extend past the edge.
            std::uint32_t cursor = 0u;
            for (const char32_t codepoint : lines_[row])
            {
                const GlyphCell &glyph =
                    glyphs.Find(static_cast<std::uint32_t>(codepoint));
                if (cursor + glyph.advance > matrix.Width())
                {
                    break;
                }
                matrix.StampGlyph(glyph, cursor, row * kGlyphRows, MergeOp::Replace);
                cursor += glyph.advance;
            }
        }

        return matrix;
    }
}
