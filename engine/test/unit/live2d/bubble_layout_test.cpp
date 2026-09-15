#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "bubble_layout.h"

namespace kpengine::live2d
{
    namespace
    {
        BubbleLayoutRequest MakeRequest()
        {
            BubbleLayoutRequest request;
            request.text_width = 32u;
            request.text_height = 16u;
            return request;
        }

        BubbleLayout Build(const BubbleTail tail)
        {
            BubbleLayoutRequest request = MakeRequest();
            request.tail = tail;
            BubbleLayout layout;
            EXPECT_TRUE(BuildBubbleLayout(request, layout));
            return layout;
        }

        float Distance(const BubbleTailCapsule &capsule, const float x, const float y)
        {
            return std::sqrt(((capsule.x - x) * (capsule.x - x)) +
                             ((capsule.y - y) * (capsule.y - y)));
        }
    }

    TEST(BubbleLayoutTest, SizesItselfToTheTextItMustHold)
    {
        // A wide line gives a wide bubble and a tall block gives a tall one. The
        // bubble hugs its content rather than being a fixed shape.
        BubbleLayoutRequest wide = MakeRequest();
        wide.text_width = 64u;
        BubbleLayout wide_layout;
        ASSERT_TRUE(BuildBubbleLayout(wide, wide_layout));
        EXPECT_GT(wide_layout.Aspect(), 1.0f);

        BubbleLayoutRequest tall = MakeRequest();
        tall.text_width = 8u;
        BubbleLayout tall_layout;
        ASSERT_TRUE(BuildBubbleLayout(tall, tall_layout));
        EXPECT_LT(tall_layout.Aspect(), 1.0f);
    }

    TEST(BubbleLayoutTest, RejectsTextWithNoExtent)
    {
        BubbleLayoutRequest request = MakeRequest();
        request.text_width = 0u;
        BubbleLayout layout;
        EXPECT_FALSE(BuildBubbleLayout(request, layout));

        request = MakeRequest();
        request.text_height = 0u;
        EXPECT_FALSE(BuildBubbleLayout(request, layout));
    }

    TEST(BubbleLayoutTest, TheBodyLeavesRoomForTheTailOnTheTailSide)
    {
        // The quad is the body plus the tail, so the body stops short of the
        // quad on the side the tail leaves by and reaches it on the other three.
        const BubbleLayout down = Build(BubbleTail::Down);
        EXPECT_LT(down.body_max_y, down.quad_height);
        EXPECT_FLOAT_EQ(down.body_min_y, 0.0f);
        EXPECT_FLOAT_EQ(down.body_min_x, 0.0f);
        EXPECT_FLOAT_EQ(down.body_max_x, down.quad_width);

        const BubbleLayout up = Build(BubbleTail::Up);
        EXPECT_GT(up.body_min_y, 0.0f);
        EXPECT_FLOAT_EQ(up.body_max_y, up.quad_height);

        const BubbleLayout left = Build(BubbleTail::Left);
        EXPECT_GT(left.body_min_x, 0.0f);
        EXPECT_FLOAT_EQ(left.body_max_x, left.quad_width);

        const BubbleLayout right = Build(BubbleTail::Right);
        EXPECT_LT(right.body_max_x, right.quad_width);
        EXPECT_FLOAT_EQ(right.body_min_x, 0.0f);
    }

    TEST(BubbleLayoutTest, NoTailLeavesTheQuadToTheBody)
    {
        const BubbleLayout layout = Build(BubbleTail::None);
        EXPECT_FLOAT_EQ(layout.body_min_x, 0.0f);
        EXPECT_FLOAT_EQ(layout.body_min_y, 0.0f);
        EXPECT_FLOAT_EQ(layout.body_max_x, layout.quad_width);
        EXPECT_FLOAT_EQ(layout.body_max_y, layout.quad_height);
        EXPECT_EQ(layout.tail_capsule_count, 0u);
    }

    TEST(BubbleLayoutTest, TheTailStartsFlushWithTheBodySoTheShapesJoinWithoutASeam)
    {
        // The first capsule sits on the body's edge and the last sits at the
        // quad's edge along the tail's axis. A gap at the root would show as a
        // break in the outline where the two shapes meet.
        const BubbleLayout down = Build(BubbleTail::Down);
        ASSERT_EQ(down.tail_capsule_count, kBubbleTailCapsuleCount);
        EXPECT_NEAR(down.tail.front().y, down.body_max_y, 1.0e-5f);
        EXPECT_NEAR(down.tail.back().y, down.quad_height, 1.0e-5f);

        const BubbleLayout right = Build(BubbleTail::Right);
        EXPECT_NEAR(right.tail.front().x, right.body_max_x, 1.0e-5f);
        EXPECT_NEAR(right.tail.back().x, right.quad_width, 1.0e-5f);

        const BubbleLayout up = Build(BubbleTail::Up);
        EXPECT_NEAR(up.tail.front().y, up.body_min_y, 1.0e-5f);
        EXPECT_NEAR(up.tail.back().y, 0.0f, 1.0e-5f);
    }

    TEST(BubbleLayoutTest, TheTailTapersToAPoint)
    {
        const BubbleLayout layout = Build(BubbleTail::Down);
        ASSERT_EQ(layout.tail_capsule_count, kBubbleTailCapsuleCount);

        // Strictly decreasing, so the tail is a taper rather than a stub. The
        // last radius is the outline's order rather than zero, because a tail
        // that reaches zero width vanishes before it reaches its tip.
        for (std::uint32_t index = 1u; index < layout.tail_capsule_count; ++index)
        {
            EXPECT_LT(layout.tail[index].radius, layout.tail[index - 1u].radius)
                << "capsule " << index << " is not thinner than its predecessor";
        }
        EXPECT_GT(layout.tail.back().radius, 0.0f);
        EXPECT_LT(layout.tail.back().radius, layout.tail.front().radius);
    }

    TEST(BubbleLayoutTest, TheTailLeansAsItLeavesTheBody)
    {
        // A straight tail would be extruded; a lean is what makes it read as
        // drawn. The tip moves sideways from the root.
        const BubbleLayout curved = Build(BubbleTail::Down);
        const float root_x = curved.tail.front().x;
        const float tip_x = curved.tail.back().x;
        EXPECT_GT(std::abs(tip_x - root_x), 0.0f);

        BubbleLayoutRequest straight_request = MakeRequest();
        straight_request.tail_curve = 0.0f;
        BubbleLayout straight;
        ASSERT_TRUE(BuildBubbleLayout(straight_request, straight));
        EXPECT_NEAR(straight.tail.back().x, straight.tail.front().x, 1.0e-5f);
    }

    TEST(BubbleLayoutTest, TheTailCurvesRatherThanBendingAtOnePoint)
    {
        // A Bezier chain's midpoints do not lie on the straight line between its
        // ends when the control point is off it, which is the difference between
        // a curve and two straight segments.
        const BubbleLayout layout = Build(BubbleTail::Down);
        const BubbleTailCapsule &first = layout.tail.front();
        const BubbleTailCapsule &middle = layout.tail[kBubbleTailCapsuleCount / 2u];
        const BubbleTailCapsule &last = layout.tail.back();

        const float span = last.y - first.y;
        ASSERT_GT(span, 0.0f);
        const float along = (middle.y - first.y) / span;
        const float straight_x = first.x + ((last.x - first.x) * along);
        EXPECT_GT(std::abs(middle.x - straight_x), 1.0e-4f)
            << "the capsule chain is straight between its ends";
    }

    TEST(BubbleLayoutTest, TheTextRegionIsInsideTheOutlineAndThePadding)
    {
        const BubbleLayout layout = Build(BubbleTail::Down);

        // Inside the body by at least the outline, or text would land on the
        // outline it is meant to sit within.
        EXPECT_GE(layout.text_min_x, layout.body_min_x + layout.outline_width);
        EXPECT_GE(layout.text_min_y, layout.body_min_y + layout.outline_width);
        EXPECT_LE(layout.text_max_x, layout.body_max_x - layout.outline_width);
        EXPECT_LE(layout.text_max_y, layout.body_max_y - layout.outline_width);
        EXPECT_GT(layout.text_max_x, layout.text_min_x);
        EXPECT_GT(layout.text_max_y, layout.text_min_y);
    }

    TEST(BubbleLayoutTest, KeepsTheTextsProportionsSoTheDotsAreNotStretched)
    {
        // The text region's aspect has to match the text's own, or the dot grid
        // sampled into it would be squashed.
        BubbleLayoutRequest request = MakeRequest();
        request.text_width = 48u;
        request.text_height = 16u;
        BubbleLayout layout;
        ASSERT_TRUE(BuildBubbleLayout(request, layout));

        const float region = (layout.text_max_x - layout.text_min_x) /
                             (layout.text_max_y - layout.text_min_y);
        EXPECT_NEAR(region, 3.0f, 1.0e-4f);
    }

    TEST(BubbleLayoutTest, ClampsTheCornerRadiusToHalfTheShorterSide)
    {
        BubbleLayoutRequest request = MakeRequest();
        request.corner_radius = 99.0f;
        BubbleLayout layout;
        ASSERT_TRUE(BuildBubbleLayout(request, layout));

        const float width = layout.body_max_x - layout.body_min_x;
        const float height = layout.body_max_y - layout.body_min_y;
        const float half_shorter = (width < height ? width : height) * 0.5f;
        EXPECT_LE(layout.corner_radius, half_shorter);
        EXPECT_GT(layout.corner_radius, 0.0f);
    }

    TEST(BubbleLayoutTest, FoldsADirectionOntoTheDominantAxis)
    {
        EXPECT_EQ(BubbleTailFromDirection(0.0f, 1.0f), BubbleTail::Down);
        EXPECT_EQ(BubbleTailFromDirection(0.0f, -1.0f), BubbleTail::Up);
        EXPECT_EQ(BubbleTailFromDirection(1.0f, 0.0f), BubbleTail::Right);
        EXPECT_EQ(BubbleTailFromDirection(-1.0f, 0.0f), BubbleTail::Left);
        EXPECT_EQ(BubbleTailFromDirection(0.0f, 0.0f), BubbleTail::None);

        // The model is usually below and to one side; the dominant axis wins so
        // the tail does not flicker between neighbours as the model moves.
        EXPECT_EQ(BubbleTailFromDirection(0.2f, 1.0f), BubbleTail::Down);
        EXPECT_EQ(BubbleTailFromDirection(1.0f, 0.2f), BubbleTail::Right);
        EXPECT_EQ(BubbleTailFromDirection(-0.3f, 1.0f), BubbleTail::Down);
    }

    TEST(BubbleLayoutTest, IsResolutionIndependent)
    {
        // The layout is in text-height units, so how many dots the text is made
        // of does not change the bubble's shape -- only its aspect, which comes
        // from the text's own proportions.
        BubbleLayoutRequest small = MakeRequest();
        small.text_width = 16u;
        small.text_height = 8u;
        BubbleLayoutRequest large = MakeRequest();
        large.text_width = 160u;
        large.text_height = 80u;

        BubbleLayout small_layout;
        BubbleLayout large_layout;
        ASSERT_TRUE(BuildBubbleLayout(small, small_layout));
        ASSERT_TRUE(BuildBubbleLayout(large, large_layout));

        EXPECT_NEAR(small_layout.Aspect(), large_layout.Aspect(), 1.0e-5f);
        EXPECT_NEAR(small_layout.body_max_x, large_layout.body_max_x, 1.0e-5f);
        EXPECT_NEAR(small_layout.tail.front().y, large_layout.tail.front().y, 1.0e-5f);
    }
}
