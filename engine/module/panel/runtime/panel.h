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

        // Replaces one row. Content wider than the panel is clipped at the
        // right edge rather than wrapped, so an overflowing string cannot
        // overwrite the row beneath it. A row outside the panel is ignored.
        void SetText(std::uint32_t row, std::string_view utf8);

        // The row's decoded codepoints; empty for a row outside the panel.
        const std::u32string &CodepointsAt(std::uint32_t row) const noexcept;

        // Destroys the dot buffer and reconstructs it from the logical content.
        // Rebuilding twice from unchanged content is byte-identical.
        DotMatrix Rebuild(const GlyphSet &glyphs) const;

    private:
        std::uint32_t columns_ = 0u;
        std::uint32_t rows_ = 0u;
        std::vector<std::u32string> lines_;
    };
}

#endif
