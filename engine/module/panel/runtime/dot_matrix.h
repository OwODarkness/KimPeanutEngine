#ifndef KPENGINE_MODULE_PANEL_DOT_MATRIX_H
#define KPENGINE_MODULE_PANEL_DOT_MATRIX_H

#include <cstdint>
#include <vector>

#include "glyph_cell.h"

namespace kpengine::panel
{
    inline constexpr std::uint32_t kDotsPerByte = 8u;
    inline constexpr std::uint32_t kRowAlignmentBits = 64u;

    // How a stamp combines with what is already in the destination. It is a
    // parameter rather than an implied convention because this is the one
    // operation every layer shares, and Replace versus Or versus Xor is exactly
    // where a wrong assumption corrupts layout silently.
    enum class MergeOp : std::uint8_t
    {
        Replace, // destination <- source
        Or,      // destination |= source
        Xor,     // destination ^= source; flash and cursor blink
        AndNot,  // destination &= ~source; erases the stamped shape
    };

    // Pads a dot width to whole 64-bit words so every row starts aligned and a
    // byte-aligned stamp can never straddle a row boundary.
    std::uint32_t RowStrideBytes(std::uint32_t width) noexcept;

    // Advances are 8 or 16, so a layout cursor is always a multiple of 8. That
    // is the invariant which keeps stamping a byte-wise operation instead of a
    // bit-shift.
    constexpr bool IsByteAligned(std::uint32_t dot_column) noexcept
    {
        return dot_column % kDotsPerByte == 0u;
    }

    class DotMatrix;

    // The bounding box of a matrix's lit dots, in dots and inclusive of both
    // ends. It exists because two consumers need the same answer for different
    // reasons -- a bubble sizes itself to the text it must hold, and the renderer
    // spans its colour ramp across what was drawn -- and a second copy of this
    // scan would be a second thing to get wrong.
    struct DotBounds final
    {
        std::uint32_t left = 0u;
        std::uint32_t top = 0u;
        std::uint32_t right = 0u;
        std::uint32_t bottom = 0u;
        // True for a blank matrix, where the coordinates mean nothing.
        bool empty = true;
    };

    DotBounds LitBounds(const DotMatrix &matrix) noexcept;

    // A flat, row-major dot buffer. Characters are placements into this buffer
    // rather than storage units: a halfwidth glyph advances 8 dots while
    // occupying 16 columns of glyph storage, so character boundaries never
    // align with a fixed cell width.
    class DotMatrix
    {
    public:
        DotMatrix(std::uint32_t width, std::uint32_t height);

        std::uint32_t Width() const noexcept { return width_; }
        std::uint32_t Height() const noexcept { return height_; }
        std::uint32_t StrideBytes() const noexcept { return stride_bytes_; }

        // Out-of-range coordinates read as off rather than reading padding.
        bool TestDot(std::uint32_t column, std::uint32_t row) const noexcept;

        void Clear() noexcept;

        // Composites a glyph at a byte-aligned dot offset. A misaligned offset
        // is ignored rather than shifted: nothing in the layout produces one,
        // and writing partial bits is the failure this buffer exists to avoid.
        void StampGlyph(const GlyphCell &glyph, std::uint32_t dot_column,
                        std::uint32_t dot_row, MergeOp op) noexcept;

        const std::vector<std::uint8_t> &Bytes() const noexcept { return bits_; }

        bool operator==(const DotMatrix &other) const noexcept;
        bool operator!=(const DotMatrix &other) const noexcept
        {
            return !(*this == other);
        }

    private:
        std::uint32_t width_ = 0u;
        std::uint32_t height_ = 0u;
        std::uint32_t stride_bytes_ = 0u;
        std::vector<std::uint8_t> bits_;
    };
}

#endif
