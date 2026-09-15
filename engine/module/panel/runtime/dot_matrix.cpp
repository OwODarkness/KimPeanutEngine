#include "dot_matrix.h"

#include <algorithm>
#include <cstddef>

namespace kpengine::panel
{
    namespace
    {
        std::uint8_t MergeByte(std::uint8_t destination, std::uint8_t source,
                               MergeOp op) noexcept
        {
            switch (op)
            {
            case MergeOp::Replace:
                return source;
            case MergeOp::Or:
                return static_cast<std::uint8_t>(destination | source);
            case MergeOp::Xor:
                return static_cast<std::uint8_t>(destination ^ source);
            case MergeOp::AndNot:
                return static_cast<std::uint8_t>(
                    destination & static_cast<std::uint8_t>(~source));
            }
            return destination;
        }
    }

    std::uint32_t RowStrideBytes(std::uint32_t width) noexcept
    {
        return ((width + kRowAlignmentBits - 1u) / kRowAlignmentBits) *
               (kRowAlignmentBits / kDotsPerByte);
    }

    DotMatrix::DotMatrix(std::uint32_t width, std::uint32_t height)
        : width_(width), height_(height), stride_bytes_(RowStrideBytes(width)),
          bits_(static_cast<std::size_t>(stride_bytes_) * height, 0u)
    {
    }

    bool DotMatrix::TestDot(std::uint32_t column, std::uint32_t row) const noexcept
    {
        if (column >= width_ || row >= height_)
        {
            return false;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(row) * stride_bytes_) + (column / kDotsPerByte);
        return ((bits_[offset] >> (column % kDotsPerByte)) & 1u) != 0u;
    }

    void DotMatrix::Clear() noexcept
    {
        std::fill(bits_.begin(), bits_.end(), std::uint8_t{0u});
    }

    void DotMatrix::StampGlyph(const GlyphCell &glyph, std::uint32_t dot_column,
                               std::uint32_t dot_row, MergeOp op) noexcept
    {
        if (!IsByteAligned(dot_column))
        {
            return;
        }

        const std::uint32_t first_byte = dot_column / kDotsPerByte;
        constexpr std::uint32_t kGlyphBytes = kGlyphColumns / kDotsPerByte;

        for (std::uint32_t row = 0u; row < kGlyphRows; ++row)
        {
            const std::uint32_t target_row = dot_row + row;
            if (target_row >= height_)
            {
                break;
            }

            for (std::uint32_t index = 0u; index < kGlyphBytes; ++index)
            {
                const std::uint32_t dot = dot_column + (index * kDotsPerByte);
                const std::uint32_t byte = first_byte + index;
                // Clip at the panel edge rather than spilling into the row's
                // padding, which would leak dots past the visible width.
                if (dot >= width_ || byte >= stride_bytes_)
                {
                    continue;
                }

                const auto source = static_cast<std::uint8_t>(
                    (glyph.rows[row] >> (index * kDotsPerByte)) & 0xFFu);
                std::uint8_t &destination =
                    bits_[(static_cast<std::size_t>(target_row) * stride_bytes_) + byte];
                destination = MergeByte(destination, source, op);
            }
        }
    }

    DotBounds LitBounds(const DotMatrix &matrix) noexcept
    {
        DotBounds bounds;
        for (std::uint32_t row = 0u; row < matrix.Height(); ++row)
        {
            for (std::uint32_t column = 0u; column < matrix.Width(); ++column)
            {
                if (!matrix.TestDot(column, row))
                {
                    continue;
                }
                if (bounds.empty)
                {
                    bounds = {column, row, column, row, false};
                    continue;
                }
                bounds.left = std::min(bounds.left, column);
                bounds.top = std::min(bounds.top, row);
                bounds.right = std::max(bounds.right, column);
                bounds.bottom = std::max(bounds.bottom, row);
            }
        }
        return bounds;
    }

    bool DotMatrix::operator==(const DotMatrix &other) const noexcept
    {
        return width_ == other.width_ && height_ == other.height_ &&
               bits_ == other.bits_;
    }
}
