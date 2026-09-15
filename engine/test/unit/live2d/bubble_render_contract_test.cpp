#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "bubble_layout.h"
#include "bubble_render_contract.h"

namespace kpengine::live2d
{
    namespace
    {
        // A mask covering everything, which is what a caller that has not
        // measured its text would produce.
        BubbleMaskInfo FullMask(const std::uint32_t dots_x = 24u,
                                const std::uint32_t dots_y = 16u)
        {
            BubbleMaskInfo mask;
            mask.dots_x = dots_x;
            mask.dots_y = dots_y;
            return mask;
        }

        BubbleLayout MakeLayout(const BubbleTail tail = BubbleTail::Down)
        {
            BubbleLayoutRequest request;
            request.text_width = 48u;
            request.text_height = 16u;
            request.tail = tail;
            BubbleLayout layout;
            EXPECT_TRUE(BuildBubbleLayout(request, layout));
            return layout;
        }

        // Where a point of the quad lands in the target, normalized so one is the
        // whole target. y runs downward, as the layout and the dot grid do.
        void QuadPointToTarget(const BubblePopScale &rect, const float u, const float v,
                               float &x, float &y)
        {
            x = rect.center_x - rect.half_width + (u * rect.half_width * 2.0f);
            y = rect.center_y - rect.half_height + (v * rect.half_height * 2.0f);
        }
    }

    TEST(BubbleRenderContractTest, PlacesTheQuadWhereItWasAskedFor)
    {
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        placement.height_fraction = 0.25f;
        placement.center_x = 0.4f;
        placement.center_y = 0.3f;
        placement.target_aspect = 1.0f;

        const BubblePopScale rect = ResolveBubblePop(layout, placement);
        EXPECT_NEAR(rect.center_x, 0.4f, 1.0e-5f);
        EXPECT_NEAR(rect.center_y, 0.3f, 1.0e-5f);
        EXPECT_NEAR(rect.half_height, 0.125f, 1.0e-5f);
    }

    TEST(BubbleRenderContractTest, KeepsTheBubblesProportionsInAnyTarget)
    {
        // The placed quad's pixel aspect has to be the bubble's own, or the
        // shape -- and the dots inside it -- would be stretched by whatever the
        // target's shape happens to be.
        const BubbleLayout layout = MakeLayout();

        for (const float target_aspect : {1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 0.75f})
        {
            BubblePlacement placement;
            placement.height_fraction = 0.2f;
            placement.target_aspect = target_aspect;

            const BubblePopScale rect = ResolveBubblePop(layout, placement);
            // Back to pixels: width_px = half_width * 2 * target_width, and the
            // target's width is its aspect times its height.
            const float width_px = rect.half_width * 2.0f * target_aspect;
            const float height_px = rect.half_height * 2.0f;
            EXPECT_NEAR(width_px / height_px, layout.Aspect(), 1.0e-4f)
                << "target aspect " << target_aspect;
        }
    }

    TEST(BubbleRenderContractTest, ThePopInGrowsOutOfTheTailRoot)
    {
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        placement.height_fraction = 0.3f;
        placement.center_x = 0.5f;
        placement.center_y = 0.4f;
        placement.target_aspect = 1.0f;

        // The tail's root, in the quad's own normalized coordinates.
        const float root_u = layout.tail.front().x / layout.quad_width;
        const float root_v = layout.tail.front().y / layout.quad_height;

        const BubblePopScale full = ResolveBubblePop(layout, placement);
        float full_root_x = 0.0f;
        float full_root_y = 0.0f;
        QuadPointToTarget(full, root_u, root_v, full_root_x, full_root_y);

        for (const float progress : {0.25f, 0.5f, 0.9f, 1.0f})
        {
            BubblePlacement animated = placement;
            animated.progress = progress;
            const BubblePopScale rect = ResolveBubblePop(layout, animated);

            // Smaller while it grows...
            EXPECT_LE(rect.half_height, full.half_height + 1.0e-5f);

            // ...but the root has not moved, which is what makes the bubble pop
            // out of the model rather than out of its own middle.
            float root_x = 0.0f;
            float root_y = 0.0f;
            QuadPointToTarget(rect, root_u, root_v, root_x, root_y);
            EXPECT_NEAR(root_x, full_root_x, 1.0e-5f) << "progress " << progress;
            EXPECT_NEAR(root_y, full_root_y, 1.0e-5f) << "progress " << progress;
        }
    }

    TEST(BubbleRenderContractTest, NoProgressIsNoSize)
    {
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        placement.progress = 0.0f;

        const BubblePopScale rect = ResolveBubblePop(layout, placement);
        EXPECT_FLOAT_EQ(rect.half_width, 0.0f);
        EXPECT_FLOAT_EQ(rect.half_height, 0.0f);
    }

    TEST(BubbleRenderContractTest, ClampsProgressRatherThanExtrapolating)
    {
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        placement.height_fraction = 0.2f;

        BubblePlacement overshoot = placement;
        overshoot.progress = 4.0f;
        const BubblePopScale full = ResolveBubblePop(layout, placement);
        const BubblePopScale clamped = ResolveBubblePop(layout, overshoot);
        EXPECT_FLOAT_EQ(clamped.half_height, full.half_height);

        BubblePlacement negative = placement;
        negative.progress = -1.0f;
        EXPECT_FLOAT_EQ(ResolveBubblePop(layout, negative).half_height, 0.0f);
    }

    TEST(BubbleRenderContractTest, FlipsYForClipSpace)
    {
        // A placement is given with y downward, as the layout and the dot grid
        // are, while clip space has y upward. Getting this wrong puts the bubble
        // on the opposite side of the model.
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        placement.height_fraction = 0.25f;
        placement.center_x = 0.5f;
        placement.center_y = 0.25f;
        placement.target_aspect = 1.0f;

        const std::array<float, 16> matrix = MakeBubblePlacement(layout, placement);
        // Column-major: the translation is the fourth column.
        EXPECT_NEAR(matrix[12], 0.0f, 1.0e-5f);
        // A quarter of the way down from the top is three quarters of the way up
        // in clip space.
        EXPECT_NEAR(matrix[13], 0.5f, 1.0e-5f);
    }

    TEST(BubbleRenderContractTest, PacksTheLayoutAndTheTailIntotheConstants)
    {
        const BubbleLayout layout = MakeLayout(BubbleTail::Right);
        BubblePlacement placement;
        BubbleAppearance appearance;

        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, FullMask());

        EXPECT_FLOAT_EQ(constants.quad_size[0], layout.quad_width);
        EXPECT_FLOAT_EQ(constants.quad_size[1], layout.quad_height);
        EXPECT_FLOAT_EQ(constants.body[0], layout.body_min_x);
        EXPECT_FLOAT_EQ(constants.body[3], layout.body_max_y);
        EXPECT_FLOAT_EQ(constants.text[2], layout.text_max_x);
        EXPECT_FLOAT_EQ(constants.shape[0], layout.corner_radius);
        EXPECT_FLOAT_EQ(constants.shape[1], layout.outline_width);
        EXPECT_FLOAT_EQ(constants.shape[2],
                        static_cast<float>(layout.tail_capsule_count));

        for (std::uint32_t index = 0u; index < layout.tail_capsule_count; ++index)
        {
            EXPECT_FLOAT_EQ(constants.tail[index][0], layout.tail[index].x);
            EXPECT_FLOAT_EQ(constants.tail[index][2], layout.tail[index].radius);
        }
        // Everything past the count is zero, so the shader's loop bound cannot
        // read a stale capsule.
        for (std::uint32_t index = layout.tail_capsule_count;
             index < kBubbleTailCapsuleCount; ++index)
        {
            EXPECT_FLOAT_EQ(constants.tail[index][2], 0.0f);
        }
    }

    TEST(BubbleRenderContractTest, LinearisesTheColorsForAnSrgbTarget)
    {
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        BubbleAppearance appearance;
        appearance.fill_color = {1.0f, 0.5f, 0.0f, 1.0f};

        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, FullMask());

        // White and black are fixed points; mid grey is not.
        EXPECT_FLOAT_EQ(constants.fill_color[0], 1.0f);
        EXPECT_FLOAT_EQ(constants.fill_color[2], 0.0f);
        EXPECT_NEAR(constants.fill_color[1], 0.214041f, 1.0e-5f);
    }

    TEST(BubbleRenderContractTest, SizesTheInteriorGridToOneCellPerTextDot)
    {
        // The grid's pitch comes from the region the layout reserved and the
        // number of dots the text is made of. Anything else and the text lands
        // across cells instead of inside them, which is what makes the interior
        // read as a matrix aligned with its content rather than as a texture
        // laid over it.
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        BubbleAppearance appearance;
        appearance.dot_gap = 0.25f;

        // The layout's text is 48 by 16 dots, so the mask that produced it is
        // too. A dot count that disagreed with the layout would give cells of the
        // wrong pitch in one axis and stretch every character.
        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, FullMask(48u, 16u));

        const float region_width = layout.text_max_x - layout.text_min_x;
        const float region_height = layout.text_max_y - layout.text_min_y;
        EXPECT_NEAR(constants.grid[0], region_width / 48.0f, 1.0e-6f);
        EXPECT_NEAR(constants.grid[1], region_height / 16.0f, 1.0e-6f);
        EXPECT_FLOAT_EQ(constants.grid[2], 0.25f);

        // Square cells: the region carries the text's own aspect, so dividing it
        // by the dot count gives the same pitch on both axes. Unequal pitches
        // would make the bezel lopsided and the dot matrix look stretched.
        EXPECT_NEAR(constants.grid[0], constants.grid[1], 1.0e-6f);
    }

    TEST(BubbleRenderContractTest, ATextMaskWithNoDotsStillGetsAUsableGrid)
    {
        // A zero dot count would divide to infinity and make the interior a
        // single flat colour with no cells at all.
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        BubbleAppearance appearance;

        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, FullMask(0u, 0u));
        EXPECT_GT(constants.grid[0], 0.0f);
        EXPECT_GT(constants.grid[1], 0.0f);
        EXPECT_TRUE(std::isfinite(constants.grid[0]));
        EXPECT_TRUE(std::isfinite(constants.grid[1]));
    }

    TEST(BubbleRenderContractTest, LinearisesTheBezelColourToo)
    {
        const BubbleLayout layout = MakeLayout();
        BubblePlacement placement;
        BubbleAppearance appearance;
        appearance.bezel_color = {0.5f, 1.0f, 0.0f, 1.0f};

        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, FullMask());
        EXPECT_NEAR(constants.bezel_color[0], 0.214041f, 1.0e-5f);
        EXPECT_FLOAT_EQ(constants.bezel_color[1], 1.0f);
    }

    TEST(BubbleRenderContractTest, ReadsTheTextFromThePartOfTheMaskItOccupies)
    {
        // A mask is a whole panel wide with the text somewhere in it. Reading the
        // mask's full extent for the bubble's text region squeezes the text into
        // a corner of the bubble -- which is what the first live run did, and why
        // the first capture had a bubble with no text in it.
        const std::array<float, 4> uv = MakeInkUv(24u, 3u, 47u, 18u, 512u, 256u);
        EXPECT_NEAR(uv[0], 24.0f / 512.0f, 1.0e-6f);
        EXPECT_NEAR(uv[1], 3.0f / 256.0f, 1.0e-6f);
        // The maximum is exclusive, so a single lit dot still spans one texel.
        EXPECT_NEAR(uv[2], 48.0f / 512.0f, 1.0e-6f);
        EXPECT_NEAR(uv[3], 19.0f / 256.0f, 1.0e-6f);
    }

    TEST(BubbleRenderContractTest, ABlankMaskFallsBackToAllOfIt)
    {
        // There is no text to find, and reading the whole mask is what a caller
        // that never measured anything expects.
        const std::array<float, 4> uv = MakeInkUv(0u, 0u, 0u, 0u, 0u, 0u);
        EXPECT_FLOAT_EQ(uv[0], 0.0f);
        EXPECT_FLOAT_EQ(uv[2], 1.0f);
    }

    TEST(BubbleRenderContractTest, NoTailMeansNoCapsulesAndAGrowthFromTheCentre)
    {
        const BubbleLayout layout = MakeLayout(BubbleTail::None);
        BubblePlacement placement;
        BubbleAppearance appearance;
        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, FullMask());

        EXPECT_FLOAT_EQ(constants.shape[2], 0.0f);
        // The pop then grows from the bubble's centre, which is the only sensible
        // reading of a shape with no far end.
        const BubblePopScale rect = ResolveBubblePop(layout, placement);
        EXPECT_NEAR(rect.center_x, placement.center_x, 1.0e-5f);
        EXPECT_NEAR(rect.center_y, placement.center_y, 1.0e-5f);
    }
}
