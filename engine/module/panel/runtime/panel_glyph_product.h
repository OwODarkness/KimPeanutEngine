#ifndef KPENGINE_MODULE_PANEL_GLYPH_PRODUCT_H
#define KPENGINE_MODULE_PANEL_GLYPH_PRODUCT_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "glyph_cell.h"

namespace kpengine::panel
{
    // Eight bytes, following the .live2d product's magic convention.
    inline constexpr std::array<char, 8> kPanelGlyphProductMagic{
        {'K', 'P', 'P', 'N', 'L', 'G', 'L', 'Y'}};
    inline constexpr std::uint32_t kPanelGlyphProductVersion = 1u;

    // The baked form of a glyph set: one contiguous codepoint range plus the
    // glyph drawn for anything outside it.
    struct PanelGlyphProduct final
    {
        std::uint32_t first_codepoint = 0u;
        std::vector<GlyphCell> glyphs;
        GlyphCell missing{};
    };

    GlyphSet ToGlyphSet(PanelGlyphProduct product);

    // Both throw std::runtime_error on an unreadable path, a bad magic or
    // version, a truncated body, or an empty range. Fields are written
    // little-endian explicitly rather than as raw struct bytes, so the product
    // does not depend on padding or host byte order.
    void WritePanelGlyphProduct(const PanelGlyphProduct &product,
                                const std::filesystem::path &path);
    PanelGlyphProduct ReadPanelGlyphProduct(const std::filesystem::path &path);
}

#endif
