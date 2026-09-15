#ifndef KPENGINE_LIVE2D_BUBBLE_RENDER_CONTRACT_H
#define KPENGINE_LIVE2D_BUBBLE_RENDER_CONTRACT_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "bubble_layout.h"

namespace kpengine::live2d
{
    inline constexpr std::uint32_t kBubbleConstantsBinding = 0u;
    inline constexpr std::uint32_t kBubbleTextBinding = 1u;

    // Where the bubble goes and how big it is. Kept apart from the layout
    // because the layout is a property of the text and this is a property of the
    // frame: the same bubble can be placed differently without being rebuilt.
    struct BubblePlacement final
    {
        // The bubble's height as a fraction of the target's height, so it keeps
        // its proportions when the target resizes.
        float height_fraction = 0.22f;
        // The bubble's centre, normalized in the target with y downward.
        float center_x = 0.5f;
        float center_y = 0.30f;
        // The target's width divided by its height, which is what turns the
        // bubble's own aspect into a placement that does not distort it.
        float target_aspect = 1.0f;
        // 0 is nothing and 1 is the finished bubble. Values between grow it out
        // of its tail root, so it pops toward whatever the tail points at rather
        // than fading in.
        float progress = 1.0f;
    };

    struct BubbleAppearance final
    {
        // Linear, not display space: the planner linearises what a caller picks,
        // because the target is sRGB and the hardware encodes on store.
        //
        // The paper: the bubble's surface, and the colour a dot's cell takes when
        // it is not lit.
        std::array<float, 4> fill_color{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> outline_color{0.05f, 0.05f, 0.06f, 1.0f};
        // The colour of a lit dot: the text.
        std::array<float, 4> dot_color{0.05f, 0.05f, 0.06f, 1.0f};
        // The gap between cells. Without it the paper is a flat field and the
        // text reads as ink on a page rather than as a matrix of elements; with
        // it every dot, lit or not, has a visible boundary.
        std::array<float, 4> bezel_color{0.82f, 0.82f, 0.84f, 1.0f};
        // Fraction of a cell left as bezel on every side.
        float dot_gap = 0.18f;
    };

    // What the shader needs to know about the text mask: where the text sits
    // inside it, and how many dots it is made of. The dot count is what turns the
    // bubble's interior into a grid whose cells line up with the text, rather
    // than a grid of some other pitch laid over it.
    struct BubbleMaskInfo final
    {
        std::array<float, 4> uv{0.0f, 0.0f, 1.0f, 1.0f};
        std::uint32_t dots_x = 1u;
        std::uint32_t dots_y = 1u;
    };

    // Mirrors the std140 block in asset/shader/bubble.vert and bubble.frag. The
    // producer writes raw bytes and the shader reads them back by offset, so the
    // layout is frozen here rather than trusted.
    struct BubbleDrawConstants final
    {
        // Column-major, and the same convention Live2D's model_transform uses.
        std::array<float, 16> placement{};
        // The quad's size in bubble units, plus the aspect in z.
        std::array<float, 4> quad_size{};
        // Body and text regions, in bubble units, as min x, min y, max x, max y.
        std::array<float, 4> body{};
        std::array<float, 4> text{};
        // Corner radius, outline width, tail capsule count, reserved.
        std::array<float, 4> shape{};
        std::array<float, 4> fill_color{};
        std::array<float, 4> outline_color{};
        std::array<float, 4> dot_color{};
        // Each tail capsule as x, y, radius, reserved.
        std::array<std::array<float, 4>, kBubbleTailCapsuleCount> tail{};
        // Where the text actually is inside the mask, as min u, min v, max u, max
        // v. A text mask is the whole panel's worth of dots with the text
        // somewhere in it, so mapping the bubble's text region onto the mask's
        // full extent would squeeze the text into a corner of the bubble.
        std::array<float, 4> ink_uv{0.0f, 0.0f, 1.0f, 1.0f};
        // The grid the interior is drawn as: cell width and height in bubble
        // units, the bezel fraction, and a reserved lane.
        std::array<float, 4> grid{1.0f, 1.0f, 0.0f, 0.0f};
        std::array<float, 4> bezel_color{0.82f, 0.82f, 0.84f, 1.0f};
    };

    static_assert(sizeof(BubbleDrawConstants) == 352u,
                  "BubbleDrawConstants must match the std140 block in bubble");
    static_assert(offsetof(BubbleDrawConstants, quad_size) == 64u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");
    static_assert(offsetof(BubbleDrawConstants, body) == 80u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");
    static_assert(offsetof(BubbleDrawConstants, shape) == 112u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");
    static_assert(offsetof(BubbleDrawConstants, tail) == 176u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");
    static_assert(offsetof(BubbleDrawConstants, ink_uv) == 304u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");
    static_assert(offsetof(BubbleDrawConstants, grid) == 320u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");
    static_assert(offsetof(BubbleDrawConstants, bezel_color) == 336u,
                  "BubbleDrawConstants members moved; update bubble.vert/frag");

    // The lit extent of a mask as normalized texture coordinates, so the shader
    // can read the text rather than the whole mask. The maximum is exclusive, so
    // a single lit dot still spans one texel rather than collapsing to nothing.
    std::array<float, 4> MakeInkUv(std::uint32_t left, std::uint32_t top,
                                   std::uint32_t right, std::uint32_t bottom,
                                   std::uint32_t width, std::uint32_t height) noexcept;

    // The clip-space placement for a bubble quad. The quad is authored in clip
    // space like the panel's, so this maps it onto the bubble's rect rather than
    // resizing any geometry -- and a matrix rather than a viewport because the
    // two backends measure a viewport's y from opposite ends, so a sub-rect
    // placed that way would be vertically mirrored between them.
    std::array<float, 16> MakeBubblePlacement(const BubbleLayout &layout,
                                              const BubblePlacement &placement) noexcept;

    // The rect the bubble's quad ends up covering, in target-normalized units
    // where one is the whole target. Resolved separately from the matrix so the
    // pop-in can be tested without one: it is the half-size and centre after the
    // growth is applied, which is where the fixed tail root and the aspect
    // correction both live.
    struct BubblePopScale final
    {
        float half_width = 0.0f;
        float half_height = 0.0f;
        float center_x = 0.5f;
        float center_y = 0.5f;
    };

    BubblePopScale ResolveBubblePop(const BubbleLayout &layout,
                                    const BubblePlacement &placement) noexcept;

    // Fills a constants block from a layout and an appearance. Separated from the
    // draw so the packing is testable with no device.
    BubbleDrawConstants MakeBubbleConstants(const BubbleLayout &layout,
                                            const BubblePlacement &placement,
                                            const BubbleAppearance &appearance,
                                            const BubbleMaskInfo &mask) noexcept;
}

#endif
