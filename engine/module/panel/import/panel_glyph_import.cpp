#include "panel_glyph_import.h"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

// Upstream stb_truetype, shipped unmodified inside the vendored ImGui tree. It
// is the only rasteriser reachable here: FreeType is not a dependency, and a
// separate stb_truetype copy cannot be fetched into third_party/. STBTT_STATIC
// gives every symbol internal linkage so a binary that also links ImGui cannot
// collide with it.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace kpengine::panel
{
    namespace
    {
        std::vector<std::uint8_t> ReadFontBytes(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw std::runtime_error("panel glyph import: cannot read font '" +
                                         path.string() + "'");
            }
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(stream),
                                             std::istreambuf_iterator<char>()};
        }

        // A hollow box, so an uncovered codepoint is visible rather than
        // silently blank.
        GlyphCell MakeTofuGlyph()
        {
            GlyphCell glyph;
            glyph.advance = kFullwidthAdvance;
            for (std::uint32_t column = 0u; column < kGlyphColumns; ++column)
            {
                glyph.SetDot(0u, column, true);
                glyph.SetDot(kGlyphRows - 1u, column, true);
            }
            for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
            {
                glyph.SetDot(row, 0u, true);
                glyph.SetDot(row, kGlyphColumns - 1u, true);
            }
            return glyph;
        }

        GlyphCell BakeOneCodepoint(const stbtt_fontinfo &info, std::uint32_t codepoint,
                                   float scale, std::int32_t baseline_row,
                                   std::uint8_t threshold, GlyphBakeResult &result)
        {
            GlyphCell glyph;
            const int glyph_index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
            if (glyph_index == 0)
            {
                // Unassigned in this face: blank, occupying a full cell.
                glyph.advance = kFullwidthAdvance;
                return glyph;
            }

            int advance_units = 0;
            int left_side_bearing = 0;
            stbtt_GetGlyphHMetrics(&info, glyph_index, &advance_units, &left_side_bearing);
            const auto advance_dots =
                static_cast<std::uint32_t>(std::lround(advance_units * scale));
            glyph.advance = advance_dots >= kFullwidthAdvanceThreshold ? kFullwidthAdvance
                                                                      : kHalfwidthAdvance;

            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetGlyphBitmapBox(&info, glyph_index, scale, scale, &x0, &y0, &x1, &y1);
            const int width = x1 - x0;
            const int height = y1 - y0;
            if (width <= 0 || height <= 0)
            {
                // A blank glyph that still advances, such as a space.
                return glyph;
            }

            std::vector<std::uint8_t> coverage(
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u);
            stbtt_MakeGlyphBitmap(&info, coverage.data(), width, height, width, scale,
                                  scale, glyph_index);

            // The box is relative to the baseline, so the baseline is what
            // places a glyph vertically. Ink outside the cell is dropped rather
            // than wrapped, so it is counted: a bake that quietly loses the
            // descenders still looks correct at a glance.
            bool dropped_ink = false;
            for (int y = 0; y < height; ++y)
            {
                const int target_row = baseline_row + y0 + y;
                const bool row_inside = target_row >= 0 &&
                                        target_row < static_cast<int>(kGlyphRows);
                for (int x = 0; x < width; ++x)
                {
                    if (coverage[(static_cast<std::size_t>(y) * width) + x] < threshold)
                    {
                        continue;
                    }
                    const int target_column = x0 + x;
                    const bool column_inside =
                        target_column >= 0 &&
                        target_column < static_cast<int>(kGlyphColumns);
                    if (!row_inside || !column_inside)
                    {
                        dropped_ink = true;
                        continue;
                    }
                    glyph.SetDot(static_cast<std::uint32_t>(target_row),
                                 static_cast<std::uint32_t>(target_column), true);
                }
            }
            if (dropped_ink)
            {
                ++result.clipped_glyphs;
            }

            // A halfwidth glyph that keeps ink in columns 8..15 would bleed into
            // the character after it, so the halfwidth invariant wins over the
            // rasterizer's output.
            if (glyph.IsHalfwidth() && !glyph.HasCleanHalfwidth())
            {
                for (std::uint16_t &row : glyph.rows)
                {
                    row = static_cast<std::uint16_t>(row & 0x00FFu);
                }
                ++result.clipped_halfwidth_glyphs;
            }
            return glyph;
        }
    }

    GlyphBakeResult BakeGlyphProduct(const GlyphBakeRequest &request)
    {
        if (request.first_codepoint > request.last_codepoint)
        {
            throw std::runtime_error("panel glyph import: codepoint range is reversed");
        }
        if (request.pixel_height == 0u)
        {
            throw std::runtime_error("panel glyph import: pixel height must be non-zero");
        }

        const std::vector<std::uint8_t> font_bytes = ReadFontBytes(request.font_path);
        if (font_bytes.empty())
        {
            throw std::runtime_error("panel glyph import: font '" +
                                     request.font_path.string() + "' is empty");
        }

        stbtt_fontinfo info{};
        const int offset = stbtt_GetFontOffsetForIndex(font_bytes.data(),
                                                       static_cast<int>(request.face_index));
        if (offset < 0 || stbtt_InitFont(&info, font_bytes.data(), offset) == 0)
        {
            throw std::runtime_error("panel glyph import: cannot initialise face " +
                                     std::to_string(request.face_index) + " of '" +
                                     request.font_path.string() + "'");
        }

        const float scale = stbtt_ScaleForMappingEmToPixels(
            &info, static_cast<float>(request.pixel_height));

        std::int32_t baseline_row = request.baseline_row;
        if (baseline_row < 0)
        {
            int ascent = 0;
            int descent = 0;
            int line_gap = 0;
            stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
            baseline_row = static_cast<std::int32_t>(std::lround(ascent * scale));
        }

        // A baseline outside the cell means the face's em does not map onto
        // these many dot rows: every glyph ends up clipped at the bottom, and
        // since the whole set clips consistently it still looks plausible in
        // aggregate. Fail instead, and let the caller pick the row.
        if (baseline_row < 1 ||
            baseline_row > static_cast<std::int32_t>(request.pixel_height) - 1)
        {
            throw std::runtime_error(
                "panel glyph import: baseline row " + std::to_string(baseline_row) +
                " does not fall inside a " + std::to_string(request.pixel_height) +
                "-row cell for '" + request.font_path.string() +
                "'; pass an explicit baseline row");
        }

        GlyphBakeResult result;
        result.baseline_row = baseline_row;
        result.product.first_codepoint = request.first_codepoint;
        result.product.missing = MakeTofuGlyph();

        const std::uint32_t count =
            request.last_codepoint - request.first_codepoint + 1u;
        result.product.glyphs.reserve(count);
        for (std::uint32_t codepoint = request.first_codepoint;
             codepoint <= request.last_codepoint; ++codepoint)
        {
            result.product.glyphs.push_back(BakeOneCodepoint(
                info, codepoint, scale, baseline_row, request.coverage_threshold,
                result));
        }

        return result;
    }
}
