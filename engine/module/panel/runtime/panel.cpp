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
        for (Line &line : lines_)
        {
            line.codepoints.clear();
            line.origin = 0u;
        }
    }

    void Panel::SetText(std::uint32_t row, std::string_view utf8)
    {
        SetTextAt(row, 0u, utf8);
    }

    void Panel::SetTextAt(std::uint32_t row, std::uint32_t column,
                          std::string_view utf8)
    {
        if (row >= rows_)
        {
            return;
        }
        lines_[row].codepoints = DecodeUtf8(utf8);
        lines_[row].origin = column;
    }

    std::uint32_t Panel::OriginAt(std::uint32_t row) const noexcept
    {
        return row >= rows_ ? 0u : lines_[row].origin;
    }

    const std::u32string &Panel::CodepointsAt(std::uint32_t row) const noexcept
    {
        static const std::u32string kEmpty;
        if (row >= rows_)
        {
            return kEmpty;
        }
        return lines_[row].codepoints;
    }

    DotMatrix Panel::Rebuild(const GlyphSet &glyphs) const
    {
        DotMatrix matrix(PanelDotWidth(columns_), PanelDotHeight(rows_));

        // Compared against the width before multiplying, so an absurd origin
        // cannot overflow the cursor.
        const std::uint32_t maximum_column = matrix.Width() / kHalfwidthAdvance;

        for (std::uint32_t row = 0u; row < rows_; ++row)
        {
            if (lines_[row].origin > maximum_column)
            {
                continue;
            }

            // The origin is a whole number of halfwidth steps, so the cursor
            // stays byte-aligned for the entire row.
            std::uint32_t cursor = lines_[row].origin * kHalfwidthAdvance;
            for (const char32_t codepoint : lines_[row].codepoints)
            {
                // Clipped against the glyph's advance rather than its stored
                // width, so a halfwidth glyph that fits by advance is still
                // drawn even though its 16 columns of storage extend past the
                // edge.
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
