#ifndef KPENGINE_EDITOR_LAYOUT_RECT_H
#define KPENGINE_EDITOR_LAYOUT_RECT_H

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

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

    inline std::vector<EditorRect> SubtractOcclusions(
        const EditorRect &source, const std::span<const EditorRect> occlusions)
    {
        std::vector<EditorRect> exposed;
        if (source.IsEmpty()) return exposed;
        exposed.push_back(source);
        for (const EditorRect &blocker : occlusions)
        {
            std::vector<EditorRect> next;
            for (const EditorRect &piece : exposed)
            {
                if (!piece.Overlaps(blocker)) { next.push_back(piece); continue; }
                const float left = std::max(piece.x, blocker.x);
                const float top = std::max(piece.y, blocker.y);
                const float right = std::min(piece.x + piece.width, blocker.x + blocker.width);
                const float bottom = std::min(piece.y + piece.height, blocker.y + blocker.height);
                const EditorRect candidates[] = {
                    {piece.x, piece.y, piece.width, top - piece.y},
                    {piece.x, bottom, piece.width, piece.y + piece.height - bottom},
                    {piece.x, top, left - piece.x, bottom - top},
                    {right, top, piece.x + piece.width - right, bottom - top}};
                for (const EditorRect &candidate : candidates)
                    if (!candidate.IsEmpty()) next.push_back(candidate);
            }
            exposed = std::move(next);
            if (exposed.empty()) break;
        }
        return exposed;
    }

    inline bool IsOccluded(const float x, const float y,
                           const std::span<const EditorRect> occlusions) noexcept
    {
        for (const EditorRect &rect : occlusions)
            if (rect.Contains(x, y)) return true;
        return false;
    }
}

#endif // KPENGINE_EDITOR_LAYOUT_RECT_H
