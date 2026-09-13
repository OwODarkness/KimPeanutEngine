#ifndef KPENGINE_EDITOR_LAYOUT_RECT_H
#define KPENGINE_EDITOR_LAYOUT_RECT_H

#include <algorithm>
#include <cmath>

namespace kpengine::editor
{
    // Minimal screen-space rectangle, deliberately ImGui-free so the layout and tool-row
    // models stay testable without a frame. Width/height are extents, not max coordinates,
    // so "empty" is a non-positive extent rather than an inverted pair.
    struct EditorRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool IsEmpty() const noexcept { return width <= 0.0f || height <= 0.0f; }

        // Inclusive on both edges, matching ResolveToolRowDrop's convention.
        bool Contains(float point_x, float point_y) const noexcept
        {
            return point_x >= x && point_x <= x + width && point_y >= y &&
                   point_y <= y + height;
        }

        bool Overlaps(const EditorRect &other) const noexcept
        {
            if (IsEmpty() || other.IsEmpty())
            {
                return false;
            }
            return x < other.x + other.width && other.x < x + width &&
                   y < other.y + other.height && other.y < y + height;
        }
    };

    // Rounds an edge-aligned rect to whole pixels before it reaches ImGui, which
    // truncates window positions and sizes to integers. Rounding the two edges rather
    // than x and width independently is what makes two neighbours agree on the pixel of
    // the edge they share: at an edge of 422.4 both compute 422, where rounding the
    // width separately would leave one at 422 and the next at 423 and draw a one-pixel
    // background line down every seam.
    inline EditorRect SnapEdgesToPixels(const EditorRect &rect) noexcept
    {
        const float left = std::round(rect.x);
        const float top = std::round(rect.y);
        const float right = std::round(rect.x + rect.width);
        const float bottom = std::round(rect.y + rect.height);
        return EditorRect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    }
}

#endif // KPENGINE_EDITOR_LAYOUT_RECT_H
