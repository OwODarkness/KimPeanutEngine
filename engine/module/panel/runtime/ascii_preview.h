#ifndef KPENGINE_MODULE_PANEL_ASCII_PREVIEW_H
#define KPENGINE_MODULE_PANEL_ASCII_PREVIEW_H

#include <string>

#include "dot_matrix.h"

namespace kpengine::panel
{
    // Renders a dot buffer as text, one character per dot and one line per dot
    // row. It exists because a baked glyph is otherwise unverifiable before the
    // render stage lands: reading dots out of a byte buffer cannot tell a
    // mirrored glyph from a correct one.
    std::string ToAsciiArt(const DotMatrix &matrix, char on = '#', char off = '.');
}

#endif
