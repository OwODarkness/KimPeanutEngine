#ifndef KPENGINE_LIVE2D_BUBBLE_LAYOUT_H
#define KPENGINE_LIVE2D_BUBBLE_LAYOUT_H

#include <array>
#include <cstdint>

namespace kpengine::live2d
{
    // Which way the bubble's tail leaves the body. The tail points from the
    // bubble toward the character, so a bubble above one carries a Down tail.
    //
    // Cardinal only. A diagonal direction folds to whichever axis dominates it,
    // because a tail that jitters between two neighbours as the model moves is
    // worse than one that is simply vertical.
    enum class BubbleTail : std::uint8_t
    {
        None,
        Down,
        Left,
        Right,
        Up,
    };

    // A tail direction as plain numbers, so whatever knows where the model is
    // does not have to know about tail enumerations. x is right, y is down.
    BubbleTail BubbleTailFromDirection(float x, float y) noexcept;

    // The tail is a chain of circles rather than a triangle. A chain along a
    // curve with tapering radii is what gives a hand-drawn tail: it leaves the
    // body smoothly and comes to a point, and the shader evaluates it with the
    // same distance function it already uses for the body.
    inline constexpr std::uint32_t kBubbleTailCapsuleCount = 8u;

    struct BubbleTailCapsule final
    {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
    };

    // Bubble units: one unit is the height of the text the bubble holds. A
    // layout is therefore the same whatever the target's resolution, and the
    // shader evaluates distances in these units so a radius is a circle and not
    // an ellipse stretched by the quad's aspect.
    struct BubbleLayout final
    {
        // The quad the bubble is drawn on, in bubble units. A placement sizes it
        // so its height is a chosen fraction of the target, which keeps the
        // bubble's proportions as the target resizes.
        float quad_width = 1.0f;
        float quad_height = 1.0f;

        // The body. The tail occupies whatever the body does not.
        float body_min_x = 0.0f;
        float body_min_y = 0.0f;
        float body_max_x = 1.0f;
        float body_max_y = 1.0f;

        float corner_radius = 0.0f;
        float outline_width = 0.0f;

        // Where the text goes, so the caller can map the dot mask into it.
        float text_min_x = 0.0f;
        float text_min_y = 0.0f;
        float text_max_x = 1.0f;
        float text_max_y = 1.0f;

        std::array<BubbleTailCapsule, kBubbleTailCapsuleCount> tail{};
        std::uint32_t tail_capsule_count = 0u;

        // Width divided by height, so a placement can size the quad without
        // distorting the bubble.
        float Aspect() const noexcept
        {
            return quad_height > 0.0f ? quad_width / quad_height : 1.0f;
        }
    };

    struct BubbleLayoutRequest final
    {
        // The text's extent in panel dots. The bubble is sized to it, so it hugs
        // its content rather than being a fixed shape.
        std::uint32_t text_width = 1u;
        std::uint32_t text_height = 1u;

        BubbleTail tail = BubbleTail::Down;

        // All four are fractions of the text's height, which is the layout's
        // unit, so changing the target's resolution changes nothing.
        float padding = 0.30f;
        float outline_width = 0.10f;
        float corner_radius = 0.35f;
        // How far the tail reaches beyond the body, and how thick its root is.
        float tail_length = 0.55f;
        float tail_root_width = 0.34f;
        // How far the tail leans as it leaves the body. Zero is a straight tail;
        // a small value is what makes it read as drawn rather than extruded.
        float tail_curve = 0.12f;
    };

    // False when the text has no extent, so a caller can report that rather than
    // draw a degenerate bubble.
    bool BuildBubbleLayout(const BubbleLayoutRequest &request,
                           BubbleLayout &out) noexcept;
}

#endif
