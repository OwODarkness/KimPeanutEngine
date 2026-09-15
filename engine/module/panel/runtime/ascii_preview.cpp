#include "ascii_preview.h"

namespace kpengine::panel
{
    std::string ToAsciiArt(const DotMatrix &matrix, char on, char off)
    {
        std::string art;
        art.reserve((static_cast<std::size_t>(matrix.Width()) + 1u) * matrix.Height());

        for (std::uint32_t row = 0u; row < matrix.Height(); ++row)
        {
            for (std::uint32_t column = 0u; column < matrix.Width(); ++column)
            {
                art.push_back(matrix.TestDot(column, row) ? on : off);
            }
            art.push_back('\n');
        }
        return art;
    }
}
