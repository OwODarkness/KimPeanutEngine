#include "bubble_render_contract.h"

#include <algorithm>

#include "base/color.h"

namespace kpengine::live2d
{
    namespace
    {
        // The target is sRGB, so the hardware encodes on store and a colour
        // picked in display space has to be linearised first.
        std::array<float, 4> DisplayColorToLinear(const std::array<float, 4> &color) noexcept
        {
            return {SrgbToLinear(color[0]), SrgbToLinear(color[1]),
                    SrgbToLinear(color[2]), color[3]};
        }

        float SafeDivide(const float numerator, const float denominator) noexcept
        {
            return denominator > 1.0e-4f ? numerator / denominator : 0.0f;
        }
    }

    BubblePopScale ResolveBubblePop(const BubbleLayout &layout,
                                    const BubblePlacement &placement) noexcept
    {
        // Clamped rather than rejected: a caller animating the pop passes a value
        // through this range every frame, and the ends mean nothing and finished.
        const float scale = std::clamp(placement.progress, 0.0f, 1.0f);

        // In target-normalized units, where one is the whole target. The width
        // comes from the bubble's own aspect divided by the target's, which is
        // what keeps the bubble from being stretched by the target's shape.
        const float full_height = std::max(placement.height_fraction, 0.0f);
        const float full_width =
            full_height * layout.Aspect() / std::max(placement.target_aspect, 1.0e-4f);
        const float half_width = full_width * 0.5f;
        const float half_height = full_height * 0.5f;

        // The tail's root in the quad's own normalized coordinates: the point the
        // bubble grows out of. Without a tail the bubble grows from its centre,
        // which is the only sensible reading of a shape that has no far end.
        float root_u = 0.5f;
        float root_v = 0.5f;
        if (layout.tail_capsule_count > 0u)
        {
            root_u = SafeDivide(layout.tail.front().x, layout.quad_width);
            root_v = SafeDivide(layout.tail.front().y, layout.quad_height);
        }

        const float root_x =
            placement.center_x - half_width + (root_u * full_width);
        const float root_y =
            placement.center_y - half_height + (root_v * full_height);

        BubblePopScale resolved;
        resolved.half_width = half_width * scale;
        resolved.half_height = half_height * scale;
        // The root stays put and the centre is pulled toward it, so the bubble
        // grows out of the model rather than out of its own middle.
        resolved.center_x = root_x + ((placement.center_x - root_x) * scale);
        resolved.center_y = root_y + ((placement.center_y - root_y) * scale);
        return resolved;
    }

    std::array<float, 16> MakeBubblePlacement(const BubbleLayout &layout,
                                              const BubblePlacement &placement) noexcept
    {
        const BubblePopScale pop = ResolveBubblePop(layout, placement);

        // Clip space spans -1..1, so a target-normalized half-size doubles. y is
        // flipped because a placement is given with y downward, as the dot grid
        // and the layout are, while clip space has y upward. Column-major, the
        // same convention the model transform uses.
        const float scale_x = pop.half_width * 2.0f;
        const float scale_y = pop.half_height * 2.0f;
        const float translate_x = (pop.center_x * 2.0f) - 1.0f;
        const float translate_y = 1.0f - (pop.center_y * 2.0f);

        return {scale_x, 0.0f, 0.0f, 0.0f,
                0.0f, scale_y, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                translate_x, translate_y, 0.0f, 1.0f};
    }

    std::array<float, 4> MakeInkUv(const std::uint32_t left, const std::uint32_t top,
                                   const std::uint32_t right,
                                   const std::uint32_t bottom,
                                   const std::uint32_t width,
                                   const std::uint32_t height) noexcept
    {
        if (width == 0u || height == 0u)
        {
            return {0.0f, 0.0f, 1.0f, 1.0f};
        }
        const float inverse_width = 1.0f / static_cast<float>(width);
        const float inverse_height = 1.0f / static_cast<float>(height);
        return {static_cast<float>(left) * inverse_width,
                static_cast<float>(top) * inverse_height,
                static_cast<float>(right + 1u) * inverse_width,
                static_cast<float>(bottom + 1u) * inverse_height};
    }

    BubbleDrawConstants MakeBubbleConstants(const BubbleLayout &layout,
                                            const BubblePlacement &placement,
                                            const BubbleAppearance &appearance,
                                            const BubbleMaskInfo &mask) noexcept
    {
        BubbleDrawConstants constants;
        // The pop-in is carried entirely by the placement, so the shader draws the
        // finished bubble and the transform does the growing. That is what keeps
        // the shape's proportions exact at every point of the animation, rather
        // than resampling a shape that is being rebuilt smaller.
        constants.placement = MakeBubblePlacement(layout, placement);

        constants.quad_size = {layout.quad_width, layout.quad_height, layout.Aspect(),
                               0.0f};
        constants.body = {layout.body_min_x, layout.body_min_y, layout.body_max_x,
                          layout.body_max_y};
        constants.text = {layout.text_min_x, layout.text_min_y, layout.text_max_x,
                          layout.text_max_y};
        constants.shape = {layout.corner_radius, layout.outline_width,
                           static_cast<float>(layout.tail_capsule_count), 0.0f};
        constants.fill_color = DisplayColorToLinear(appearance.fill_color);
        constants.outline_color = DisplayColorToLinear(appearance.outline_color);
        constants.dot_color = DisplayColorToLinear(appearance.dot_color);
        constants.ink_uv = mask.uv;

        // The grid's cell is one text dot, so the text lands in cells rather
        // than across them. Derived from the region the layout reserved and the
        // number of dots the text is made of, which is the only pair that
        // guarantees the two agree.
        const float text_width = layout.text_max_x - layout.text_min_x;
        const float text_height = layout.text_max_y - layout.text_min_y;
        constants.grid = {text_width / static_cast<float>(mask.dots_x > 0u ? mask.dots_x : 1u),
                          text_height / static_cast<float>(mask.dots_y > 0u ? mask.dots_y : 1u),
                          appearance.dot_gap, 0.0f};
        constants.bezel_color = DisplayColorToLinear(appearance.bezel_color);

        for (std::uint32_t index = 0u; index < kBubbleTailCapsuleCount; ++index)
        {
            if (index >= layout.tail_capsule_count)
            {
                break;
            }
            constants.tail[index] = {layout.tail[index].x, layout.tail[index].y,
                                     layout.tail[index].radius, 0.0f};
        }

        return constants;
    }
}
