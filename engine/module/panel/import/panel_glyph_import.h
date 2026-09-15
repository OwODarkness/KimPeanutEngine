#ifndef KPENGINE_MODULE_PANEL_GLYPH_IMPORT_H
#define KPENGINE_MODULE_PANEL_GLYPH_IMPORT_H

#include <cstdint>
#include <filesystem>

#include "panel_glyph_product.h"

namespace kpengine::panel
{
    // The em square maps onto this many dot rows, so CJK fills the cell and
    // Latin shares its baseline instead of being scaled to its own bounding box.
    inline constexpr std::uint32_t kDefaultGlyphPixelHeight = 16u;

    // The rasterizer is grayscale and a panel dot is one bit, so the rasterizer
    // is quantised rather than merely sampled.
    inline constexpr std::uint8_t kDefaultCoverageThreshold = 128u;

    // An advance at or above this is fullwidth. Half of a 16-dot cell is 8, so
    // 12 sits between the two classes rather than favouring either.
    inline constexpr std::uint32_t kFullwidthAdvanceThreshold = 12u;

    struct GlyphBakeRequest final
    {
        std::filesystem::path font_path;

        // Face inside a TrueType collection; 0 for a plain .ttf.
        std::uint32_t face_index = 0u;

        std::uint32_t first_codepoint = 0u;
        std::uint32_t last_codepoint = 0u;

        // Zero means the em square maps to the cell height.
        std::uint32_t pixel_height = kDefaultGlyphPixelHeight;
        std::uint8_t coverage_threshold = kDefaultCoverageThreshold;

        // Negative derives the baseline from the font's ascent. Descenders are
        // the reason to override it: at 16 dot rows a Latin descender can fall
        // off the bottom of the cell.
        std::int32_t baseline_row = -1;
    };

    struct GlyphBakeResult final
    {
        PanelGlyphProduct product;

        // The baseline actually used, reported so a caller can see what the
        // font's metrics produced.
        std::int32_t baseline_row = 0;

        // Glyphs with ink that fell outside the cell and was dropped. Ink is
        // clipped rather than scaled, so a non-zero count is the only signal
        // that the face or the baseline does not fit the cell: the lost ink
        // clips uniformly and the result still looks plausible.
        std::uint32_t clipped_glyphs = 0u;

        // Halfwidth glyphs whose right half was cleared. Non-zero means the
        // source font draws wider than an 8-dot advance allows. This is a
        // separate count because it enforces an invariant rather than
        // reporting a loss: the cleared dots would bleed into the next
        // character.
        std::uint32_t clipped_halfwidth_glyphs = 0u;
    };

    // Throws std::runtime_error when the font cannot be read or initialised, or
    // when the range is empty or reversed.
    GlyphBakeResult BakeGlyphProduct(const GlyphBakeRequest &request);
}

#endif
