#include "bubble_layout.h"

#include <cmath>

namespace kpengine::live2d
{
    namespace
    {
        struct Point final
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        // Quadratic Bezier, which is enough curve for a tail and cheap enough to
        // evaluate on the CPU once per layout rather than per fragment.
        Point Bezier(const Point &from, const Point &control, const Point &to,
                     const float t) noexcept
        {
            const float inverse = 1.0f - t;
            return {inverse * inverse * from.x + 2.0f * inverse * t * control.x +
                        t * t * to.x,
                    inverse * inverse * from.y + 2.0f * inverse * t * control.y +
                        t * t * to.y};
        }

        // Where the tail leaves the body, and where it points. Both are on the
        // body's edge, so the chain starts flush with the outline and the union
        // of the two shapes has no visible seam.
        void TailEnds(const BubbleTail tail, const float body_min_x,
                      const float body_max_x, const float body_min_y,
                      const float body_max_y, Point &from, Point &to) noexcept
        {
            const float center_x = (body_min_x + body_max_x) * 0.5f;
            const float center_y = (body_min_y + body_max_y) * 0.5f;
            from = {center_x, center_y};
            to = {center_x, center_y};
            switch (tail)
            {
            case BubbleTail::Down:
                from = {center_x, body_max_y};
                to = {center_x, body_max_y};
                break;
            case BubbleTail::Up:
                from = {center_x, body_min_y};
                to = {center_x, body_min_y};
                break;
            case BubbleTail::Left:
                from = {body_min_x, center_y};
                to = {body_min_x, center_y};
                break;
            case BubbleTail::Right:
                from = {body_max_x, center_y};
                to = {body_max_x, center_y};
                break;
            case BubbleTail::None:
                break;
            }
        }
    }

    BubbleTail BubbleTailFromDirection(const float x, const float y) noexcept
    {
        const float ax = std::abs(x);
        const float ay = std::abs(y);
        if (ax < 1.0e-4f && ay < 1.0e-4f)
        {
            return BubbleTail::None;
        }
        if (ay >= ax)
        {
            return y >= 0.0f ? BubbleTail::Down : BubbleTail::Up;
        }
        return x >= 0.0f ? BubbleTail::Right : BubbleTail::Left;
    }

    bool BuildBubbleLayout(const BubbleLayoutRequest &request, BubbleLayout &out) noexcept
    {
        out = BubbleLayout{};
        if (request.text_width == 0u || request.text_height == 0u)
        {
            return false;
        }

        // Everything below is in bubble units, where the text's height is one
        // unit and its width is therefore its aspect.
        const float text_aspect =
            static_cast<float>(request.text_width) / static_cast<float>(request.text_height);
        const float padding = request.padding > 0.0f ? request.padding : 0.0f;
        const float outline = request.outline_width > 0.0f ? request.outline_width : 0.0f;
        // The body is sized to hold the text plus its padding *and* the outline,
        // because the outline is stroked inside the body's edge. That is what
        // makes the text region below exactly the text's own size.
        const float inset = padding + outline;
        const float body_width = text_aspect + (inset * 2.0f);
        const float body_height = 1.0f + (inset * 2.0f);

        const bool vertical_tail =
            request.tail == BubbleTail::Down || request.tail == BubbleTail::Up;
        const bool has_tail = request.tail != BubbleTail::None;
        const float tail_length = has_tail && request.tail_length > 0.0f
                                      ? request.tail_length
                                      : 0.0f;

        // The tail extends the quad along the axis it leaves by, and the body
        // keeps the full extent of the other. That is what makes a wide text give
        // a wide bubble rather than a square one with a wide letterbox inside it.
        out.quad_width = body_width + (vertical_tail ? 0.0f : tail_length);
        out.quad_height = body_height + (vertical_tail ? tail_length : 0.0f);

        switch (request.tail)
        {
        case BubbleTail::Up:
            out.body_min_y = tail_length;
            out.body_max_y = out.quad_height;
            break;
        case BubbleTail::Left:
            out.body_min_x = tail_length;
            break;
        default:
            break;
        }
        // Assigned unconditionally rather than guarded. A defaulted maximum is
        // not a computed one: `body_max_y` starts at 1.0 and a guard testing it
        // against `body_min_y` passes for the wrong reason, leaving the body an
        // arbitrary height. Both maxima are derived from a minimum plus a size,
        // which is the only way they stay consistent with the tail's geometry.
        out.body_max_x = out.body_min_x + body_width;
        out.body_max_y = out.body_min_y + body_height;

        // The outline is stroked inside the body's edge, so the body's rect is
        // the outline's outer edge and the corner radius applies to that. Clamped
        // to half the shorter side, because a radius larger than that stops being
        // a rounded rectangle at all.
        const float half_shorter =
            (body_width < body_height ? body_width : body_height) * 0.5f;
        out.corner_radius = request.corner_radius > 0.0f ? request.corner_radius : 0.0f;
        if (out.corner_radius > half_shorter)
        {
            out.corner_radius = half_shorter;
        }
        out.outline_width = outline;

        // Centred at exactly the text's own size, rather than inset from the
        // body by a constant. An equal inset on all four sides does not preserve
        // an aspect ratio: insetting a 3:1 box by the same amount on each side
        // gives something wider than 3:1, and the dot grid sampled into it would
        // come out stretched. Sizing the body from the text instead makes the
        // margins come out as the padding by construction.
        const float body_center_x = (out.body_min_x + out.body_max_x) * 0.5f;
        const float body_center_y = (out.body_min_y + out.body_max_y) * 0.5f;
        out.text_min_x = body_center_x - (text_aspect * 0.5f);
        out.text_max_x = body_center_x + (text_aspect * 0.5f);
        out.text_min_y = body_center_y - 0.5f;
        out.text_max_y = body_center_y + 0.5f;
        if (out.text_max_x <= out.text_min_x || out.text_max_y <= out.text_min_y)
        {
            return false;
        }

        if (tail_length <= 0.0f)
        {
            return true;
        }

        Point from;
        Point to;
        TailEnds(request.tail, out.body_min_x, out.body_max_x, out.body_min_y,
                 out.body_max_y, from, to);

        // Along the axis the tail leaves by, and leaning sideways as it goes.
        const float lean = request.tail_curve;
        switch (request.tail)
        {
        case BubbleTail::Down:
            to.y = out.body_max_y + tail_length;
            to.x = from.x + (body_width * lean);
            break;
        case BubbleTail::Up:
            to.y = out.body_min_y - tail_length;
            to.x = from.x + (body_width * lean);
            break;
        case BubbleTail::Left:
            to.x = out.body_min_x - tail_length;
            to.y = from.y + (body_height * lean);
            break;
        case BubbleTail::Right:
            to.x = out.body_max_x + tail_length;
            to.y = from.y + (body_height * lean);
            break;
        case BubbleTail::None:
            return true;
        }

        // The control point bends the chain, which is what makes the tail leave
        // the body along its edge rather than hinging from it.
        const Point control{(from.x + to.x) * 0.5f + (from.y - to.y) * lean,
                            (from.y + to.y) * 0.5f + (to.x - from.x) * lean};

        const float root_radius = request.tail_root_width > 0.0f
                                      ? request.tail_root_width * 0.5f
                                      : outline;
        // The tip is about as thick as the outline, so the tail comes to a point
        // that still reads at small sizes instead of vanishing.
        const float tip_radius = outline > 0.0f ? outline * 0.75f : 0.05f;

        for (std::uint32_t index = 0u; index < kBubbleTailCapsuleCount; ++index)
        {
            const float t = static_cast<float>(index) /
                            static_cast<float>(kBubbleTailCapsuleCount - 1u);
            const Point point = Bezier(from, control, to, t);
            out.tail[index] = {point.x, point.y,
                               root_radius + ((tip_radius - root_radius) * t)};
        }
        out.tail_capsule_count = kBubbleTailCapsuleCount;
        return true;
    }
}
