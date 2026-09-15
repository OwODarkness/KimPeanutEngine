#ifndef KPENGINE_MODULE_PANEL_PANEL_H
#define KPENGINE_MODULE_PANEL_PANEL_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dot_matrix.h"
#include "glyph_cell.h"

namespace kpengine::panel
{
    inline constexpr std::uint32_t kPanelColumns = 32u;
    inline constexpr std::uint32_t kPanelRows = 16u;

    constexpr std::uint32_t PanelDotWidth(std::uint32_t columns) noexcept
    {
        return columns * kGlyphColumns;
    }

    constexpr std::uint32_t PanelDotHeight(std::uint32_t rows) noexcept
    {
        return rows * kGlyphRows;
    }

    // What the panel says. This is the source of truth: the dot buffer is a
    // cache rebuilt from it and is never edited to change content, which is
    // what keeps a shorter replacement from leaving the previous text behind.
    class Panel
    {
    public:
        explicit Panel(std::uint32_t columns = kPanelColumns,
                       std::uint32_t rows = kPanelRows);

        std::uint32_t Columns() const noexcept { return columns_; }
        std::uint32_t Rows() const noexcept { return rows_; }

        void Clear() noexcept;

        // Replaces one row, laid out from the left edge.
        void SetText(std::uint32_t row, std::string_view utf8);

        // Replaces one row, laid out from `column` so a caller can centre or
        // indent without a second content model.
        //
        // `column` is measured in halfwidth steps -- one 8-dot character -- and
        // not in dots. The unit is fixed by the byte-alignment invariant: a
        // stamp at a dot offset that is not a multiple of 8 is ignored, so
        // taking raw dots here would let a caller silently draw nothing. Every
        // value of this unit is therefore representable.
        //
        // An origin past the panel edge draws nothing rather than wrapping onto
        // the row below. A row outside the panel is ignored.
        void SetTextAt(std::uint32_t row, std::uint32_t column,
                       std::string_view utf8);

        // The column SetTextAt was given, in halfwidth steps.
        std::uint32_t OriginAt(std::uint32_t row) const noexcept;

        // The row's decoded codepoints; empty for a row outside the panel.
        const std::u32string &CodepointsAt(std::uint32_t row) const noexcept;

        // Destroys the dot buffer and reconstructs it from the logical content.
        // Rebuilding twice from unchanged content is byte-identical.
        DotMatrix Rebuild(const GlyphSet &glyphs) const;

    private:
        struct Line final
        {
            std::u32string codepoints;
            std::uint32_t origin = 0u;
        };

        std::uint32_t columns_ = 0u;
        std::uint32_t rows_ = 0u;
        std::vector<Line> lines_;
    };
}

#endif
