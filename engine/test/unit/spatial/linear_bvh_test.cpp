#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "spatial/linear_bvh.h"

namespace
{
    using kpengine::Vector3f;
    using kpengine::spatial::AABB;
    using kpengine::spatial::LinearBVH;
    using kpengine::spatial::LinearBVHBuildOptions;
    using kpengine::spatial::LinearBVHNode;
    using kpengine::spatial::LinearBVHRayHit;
    using kpengine::spatial::IntersectRayAABB;
    using kpengine::spatial::kLinearBVHInvalidIndex;
    using kpengine::spatial::Ray;

    constexpr float kInfinity = std::numeric_limits<float>::infinity();

    // Fixed seeds everywhere: a property failure has to be reproducible.
    std::mt19937 MakeRandom()
    {
        return std::mt19937{20260914u};
    }

    std::vector<AABB> MakeRandomBoxes(std::mt19937 &random, std::size_t count)
    {
        std::uniform_real_distribution<float> centre(-20.0f, 20.0f);
        std::uniform_real_distribution<float> half_extent(0.2f, 2.0f);

        std::vector<AABB> boxes;
        boxes.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vector3f centre_point{centre(random), centre(random), centre(random)};
            const Vector3f extent{half_extent(random), half_extent(random), half_extent(random)};
            boxes.push_back(AABB{centre_point - extent, centre_point + extent});
        }
        return boxes;
    }

    // For tests that only need boxes and never touch the generator themselves.
    std::vector<AABB> MakeRandomBoxes(std::size_t count)
    {
        std::mt19937 random = MakeRandom();
        return MakeRandomBoxes(random, count);
    }

    Ray MakeRandomRay(std::mt19937 &random)
    {
        std::uniform_real_distribution<float> origin(-30.0f, 30.0f);
        std::uniform_real_distribution<float> direction(-1.0f, 1.0f);

        Ray ray;
        do
        {
            ray.origin = Vector3f{origin(random), origin(random), origin(random)};
            ray.direction = Vector3f{direction(random), direction(random), direction(random)};
        } while (!ray.IsValid());
        return ray;
    }

    std::optional<LinearBVHRayHit> BruteForceNearest(const Ray &ray, std::span<const AABB> boxes,
                                               float max_distance)
    {
        std::optional<LinearBVHRayHit> best;
        float closest = max_distance;
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            const std::optional<float> distance = IntersectRayAABB(ray, boxes[i]);
            if (distance.has_value() && *distance < closest)
            {
                closest = *distance;
                best = LinearBVHRayHit{static_cast<uint32_t>(i), *distance};
            }
        }
        return best;
    }

    std::vector<uint32_t> BruteForceAllHits(const Ray &ray, std::span<const AABB> boxes,
                                            float max_distance)
    {
        std::vector<uint32_t> hits;
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            const std::optional<float> distance = IntersectRayAABB(ray, boxes[i]);
            if (distance.has_value() && *distance < max_distance)
            {
                hits.push_back(static_cast<uint32_t>(i));
            }
        }
        std::sort(hits.begin(), hits.end());
        return hits;
    }

    bool Overlaps(const AABB &lhs, const AABB &rhs)
    {
        return lhs.min_.x_ <= rhs.max_.x_ && lhs.max_.x_ >= rhs.min_.x_ &&
               lhs.min_.y_ <= rhs.max_.y_ && lhs.max_.y_ >= rhs.min_.y_ &&
               lhs.min_.z_ <= rhs.max_.z_ && lhs.max_.z_ >= rhs.min_.z_;
    }

    std::vector<uint32_t> BruteForceOverlap(std::span<const AABB> boxes, const AABB &query)
    {
        std::vector<uint32_t> overlaps;
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            if (Overlaps(boxes[i], query))
            {
                overlaps.push_back(static_cast<uint32_t>(i));
            }
        }
        std::sort(overlaps.begin(), overlaps.end());
        return overlaps;
    }

    bool Contains(const AABB &outer, const AABB &inner)
    {
        return outer.min_.x_ <= inner.min_.x_ && outer.min_.y_ <= inner.min_.y_ &&
               outer.min_.z_ <= inner.min_.z_ && outer.max_.x_ >= inner.max_.x_ &&
               outer.max_.y_ >= inner.max_.y_ && outer.max_.z_ >= inner.max_.z_;
    }

    uint32_t SubtreeDepth(const LinearBVH &bvh, uint32_t index, uint32_t depth)
    {
        const LinearBVHNode &node = bvh.Nodes()[index];
        if (node.IsLeaf())
        {
            return depth;
        }
        return std::max(SubtreeDepth(bvh, node.first, depth + 1),
                        SubtreeDepth(bvh, node.right, depth + 1));
    }

    void ExpectFiniteValidBounds(const AABB &bounds)
    {
        EXPECT_TRUE(std::isfinite(bounds.min_.x_) && std::isfinite(bounds.min_.y_) &&
                    std::isfinite(bounds.min_.z_) && std::isfinite(bounds.max_.x_) &&
                    std::isfinite(bounds.max_.y_) && std::isfinite(bounds.max_.z_));
        EXPECT_TRUE(bounds.IsValid());
    }
}

TEST(LinearBVHTest, BuildsEmptyTreeForEmptyInput)
{
    const LinearBVH bvh;

    EXPECT_TRUE(bvh.IsEmpty());
    EXPECT_EQ(bvh.PrimitiveCount(), 0u);
    EXPECT_EQ(bvh.NodeCount(), 0u);
    EXPECT_FALSE(bvh.Bounds().IsValid());
    EXPECT_TRUE(bvh.Nodes().empty());
    EXPECT_TRUE(bvh.PrimitiveOrder().empty());
    EXPECT_FALSE(bvh.IntersectRay(Ray{{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}}).has_value());

    std::vector<uint32_t> overlaps;
    bvh.QueryOverlap(AABB{{-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f}}, overlaps);
    EXPECT_TRUE(overlaps.empty());
}

TEST(LinearBVHTest, EmptyInputBuildsSuccessfullyAndRejectsImpossibleOptions)
{
    LinearBVH bvh;
    const std::vector<AABB> boxes = MakeRandomBoxes(4);

    EXPECT_TRUE(bvh.Build({}));
    EXPECT_TRUE(bvh.IsEmpty());

    EXPECT_FALSE(bvh.Build(boxes, LinearBVHBuildOptions{0, 64, 12}));
    EXPECT_FALSE(bvh.Build(boxes, LinearBVHBuildOptions{4, 0, 12}));
    EXPECT_FALSE(bvh.Build(boxes, LinearBVHBuildOptions{4, 64, 1}));
    EXPECT_TRUE(bvh.IsEmpty());
}

TEST(LinearBVHTest, SinglePrimitiveBecomesRootLeaf)
{
    LinearBVH bvh;
    const std::vector<AABB> boxes{AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}};

    ASSERT_TRUE(bvh.Build(boxes));
    ASSERT_EQ(bvh.NodeCount(), 1u);

    const LinearBVHNode &root = bvh.Nodes().front();
    EXPECT_TRUE(root.IsLeaf());
    EXPECT_EQ(root.count, 1u);
    EXPECT_EQ(root.first, 0u);
    EXPECT_EQ(bvh.Bounds(), boxes.front());
}

TEST(LinearBVHTest, IndexesEveryPrimitiveExactlyOnce)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(1000);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    std::vector<uint32_t> order(bvh.PrimitiveOrder().begin(), bvh.PrimitiveOrder().end());
    ASSERT_EQ(order.size(), boxes.size());
    std::sort(order.begin(), order.end());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(order[i], i);
    }
}

TEST(LinearBVHTest, LeafRangesTileTheOrderArray)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(500);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (const LinearBVHNode &node : bvh.Nodes())
    {
        if (node.IsLeaf())
        {
            ranges.emplace_back(node.first, node.first + node.count);
        }
    }
    ASSERT_FALSE(ranges.empty());
    std::sort(ranges.begin(), ranges.end());

    uint32_t expected = 0;
    for (const auto &[begin, end] : ranges)
    {
        EXPECT_EQ(begin, expected);
        EXPECT_GT(end, begin);
        expected = end;
    }
    EXPECT_EQ(expected, boxes.size());
}

TEST(LinearBVHTest, NodeBoundsContainTheirChildrenAndPrimitives)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(500);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    for (const LinearBVHNode &node : bvh.Nodes())
    {
        ExpectFiniteValidBounds(node.bounds);
        if (node.IsLeaf())
        {
            for (uint32_t i = 0; i < node.count; ++i)
            {
                EXPECT_TRUE(Contains(node.bounds, bvh.PrimitiveBounds()[bvh.PrimitiveOrder()[node.first + i]]));
            }
            continue;
        }
        EXPECT_TRUE(Contains(node.bounds, bvh.Nodes()[node.first].bounds));
        EXPECT_TRUE(Contains(node.bounds, bvh.Nodes()[node.right].bounds));
    }

    for (const AABB &box : bvh.PrimitiveBounds())
    {
        EXPECT_TRUE(Contains(bvh.Bounds(), box));
    }
}

TEST(LinearBVHTest, ChildrenHaveHigherIndicesThanTheirParents)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(500);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    for (std::size_t i = 0; i < bvh.Nodes().size(); ++i)
    {
        const LinearBVHNode &node = bvh.Nodes()[i];
        if (node.IsLeaf())
        {
            continue;
        }
        EXPECT_GT(node.first, i);
        EXPECT_GT(node.right, node.first);
    }
}

TEST(LinearBVHTest, RespectsMaxLeafSize)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(500);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes, LinearBVHBuildOptions{4, 64, 12}));

    for (const LinearBVHNode &node : bvh.Nodes())
    {
        if (node.IsLeaf())
        {
            EXPECT_LE(node.count, 4u);
        }
    }
}

TEST(LinearBVHTest, DepthCapForcesLeavesWithoutLosingPrimitives)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(300);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes, LinearBVHBuildOptions{1, 2, 12}));

    EXPECT_LE(SubtreeDepth(bvh, 0, 0), 2u);
    EXPECT_EQ(bvh.PrimitiveCount(), boxes.size());

    std::vector<uint32_t> order(bvh.PrimitiveOrder().begin(), bvh.PrimitiveOrder().end());
    std::sort(order.begin(), order.end());
    ASSERT_EQ(order.size(), boxes.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(order[i], i);
    }
}

TEST(LinearBVHTest, CoincidentPrimitivesTerminateAndSplitEvenly)
{
    // Every centroid is identical, so no surface area split exists and the build
    // has to fall back to halving by position. A hang here fails the test.
    const std::vector<AABB> boxes(1000, AABB{{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}});
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    EXPECT_EQ(bvh.PrimitiveCount(), 1000u);
    // The balanced fallback reaches log2 depth, nowhere near the depth cap.
    EXPECT_LT(SubtreeDepth(bvh, 0, 0), 20u);
    for (const LinearBVHNode &node : bvh.Nodes())
    {
        if (node.IsLeaf())
        {
            EXPECT_LE(node.count, 4u);
        }
    }
}

TEST(LinearBVHTest, NearestRayHitMatchesBruteForceScan)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 400);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    for (int i = 0; i < 400; ++i)
    {
        const Ray ray = MakeRandomRay(random);
        const std::optional<LinearBVHRayHit> expected = BruteForceNearest(ray, boxes, kInfinity);
        const std::optional<LinearBVHRayHit> actual = bvh.IntersectRay(ray);

        ASSERT_EQ(actual.has_value(), expected.has_value());
        if (!expected.has_value())
        {
            continue;
        }

        EXPECT_FLOAT_EQ(actual->distance, expected->distance);
        // Equidistant hits may legitimately resolve to a different primitive, so
        // tie the answer down through the distance it reports for that primitive.
        const std::optional<float> own_distance =
            IntersectRayAABB(ray, bvh.PrimitiveBounds()[actual->primitive]);
        ASSERT_TRUE(own_distance.has_value());
        EXPECT_FLOAT_EQ(*own_distance, actual->distance);
    }
}

TEST(LinearBVHTest, NearestRayHitRespectsMaxDistance)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 200);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    bool saw_admitted = false;
    bool saw_excluded = false;
    for (int i = 0; i < 200; ++i)
    {
        const Ray ray = MakeRandomRay(random);
        const std::optional<LinearBVHRayHit> unbounded = bvh.IntersectRay(ray);
        if (!unbounded.has_value())
        {
            continue;
        }

        // A limit above the nearest hit must still admit it, unchanged.
        const float generous = unbounded->distance + 1.0f;
        const std::optional<LinearBVHRayHit> admitted = bvh.IntersectRay(ray, generous);
        const std::optional<LinearBVHRayHit> admitted_expected = BruteForceNearest(ray, boxes, generous);
        ASSERT_EQ(admitted.has_value(), admitted_expected.has_value());
        if (admitted.has_value())
        {
            EXPECT_FLOAT_EQ(admitted->distance, admitted_expected->distance);
        }

        // A limit below it must exclude every hit, since none can be nearer.
        const float strict = unbounded->distance * 0.5f;
        const std::optional<LinearBVHRayHit> excluded = bvh.IntersectRay(ray, strict);
        EXPECT_FALSE(excluded.has_value());
        EXPECT_FALSE(BruteForceNearest(ray, boxes, strict).has_value());

        saw_admitted = saw_admitted || admitted.has_value();
        saw_excluded = saw_excluded || !excluded.has_value();
    }
    EXPECT_TRUE(saw_admitted);
    EXPECT_TRUE(saw_excluded);
}

TEST(LinearBVHTest, AllRayHitsMatchBruteForceScan)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 300);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    for (int i = 0; i < 150; ++i)
    {
        const Ray ray = MakeRandomRay(random);
        const float max_distance = 20.0f;

        std::vector<uint32_t> actual;
        bvh.IntersectRayAll(ray, max_distance, [&](uint32_t primitive, float distance) {
            EXPECT_LT(distance, max_distance);
            actual.push_back(primitive);
        });
        std::sort(actual.begin(), actual.end());

        EXPECT_EQ(actual, BruteForceAllHits(ray, boxes, max_distance));
    }
}

TEST(LinearBVHTest, QueryOverlapMatchesBruteForceScan)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 400);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    std::uniform_real_distribution<float> centre(-20.0f, 20.0f);
    std::uniform_real_distribution<float> half_extent(0.5f, 8.0f);
    for (int i = 0; i < 150; ++i)
    {
        const Vector3f centre_point{centre(random), centre(random), centre(random)};
        const Vector3f extent{half_extent(random), half_extent(random), half_extent(random)};
        const AABB query{centre_point - extent, centre_point + extent};

        std::vector<uint32_t> actual;
        bvh.QueryOverlap(query, actual);
        std::sort(actual.begin(), actual.end());

        EXPECT_EQ(actual, BruteForceOverlap(boxes, query));
    }
}

TEST(LinearBVHTest, QueryOverlapAppendsToTheOutput)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(100);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    std::vector<uint32_t> overlaps{42u, 43u};
    bvh.QueryOverlap(AABB{{-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f}}, overlaps);

    ASSERT_GE(overlaps.size(), 2u);
    EXPECT_EQ(overlaps.front(), 42u);
    EXPECT_EQ(overlaps[1], 43u);
    EXPECT_EQ(overlaps.size(), boxes.size() + 2);
}

TEST(LinearBVHTest, ParallelRayOnSlabPlaneStillHits)
{
    // direction.x_ is exactly zero, so the reciprocal is infinite, and the origin
    // starts exactly on min_.x_: that product is 0 * inf = NaN. This is the
    // regression test for the slab test's argument ordering.
    const std::vector<AABB> boxes{AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    const Ray ray{{0.0f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    const std::optional<LinearBVHRayHit> hit = bvh.IntersectRay(ray);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->primitive, 0u);
    EXPECT_FLOAT_EQ(hit->distance, 4.0f);

    // Negative zero gives an infinite reciprocal of the opposite sign, which is
    // the other half of the same NaN case.
    const Ray negative_zero_ray{{0.0f, 0.5f, 5.0f}, {-0.0f, 0.0f, -1.0f}};
    const std::optional<LinearBVHRayHit> negative_zero_hit = bvh.IntersectRay(negative_zero_ray);
    ASSERT_TRUE(negative_zero_hit.has_value());
    EXPECT_FLOAT_EQ(negative_zero_hit->distance, 4.0f);
}

TEST(LinearBVHTest, ParallelRayOutsideSlabMisses)
{
    const std::vector<AABB> boxes{AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    const Ray ray{{5.0f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    EXPECT_FALSE(bvh.IntersectRay(ray).has_value());
}

TEST(LinearBVHTest, RayStartingInsideReturnsZeroDistance)
{
    // Parity with IntersectRayAABB's nearest non-negative convention.
    const std::vector<AABB> boxes{AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}};
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    const std::optional<LinearBVHRayHit> hit =
        bvh.IntersectRay(Ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});

    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->distance, 0.0f);
}

TEST(LinearBVHTest, RefitAfterBuildIsANoOp)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(300);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    const std::vector<LinearBVHNode> before(bvh.Nodes().begin(), bvh.Nodes().end());

    ASSERT_TRUE(bvh.Refit(boxes));
    ASSERT_EQ(bvh.Nodes().size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_EQ(bvh.Nodes()[i].bounds, before[i].bounds);
        EXPECT_EQ(bvh.Nodes()[i].first, before[i].first);
        EXPECT_EQ(bvh.Nodes()[i].count, before[i].count);
        EXPECT_EQ(bvh.Nodes()[i].right, before[i].right);
    }
}

TEST(LinearBVHTest, RefitTracksMovedPrimitivesWithoutChangingTopology)
{
    std::mt19937 random = MakeRandom();
    std::vector<AABB> boxes = MakeRandomBoxes(random, 300);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    const std::vector<uint32_t> order_before(bvh.PrimitiveOrder().begin(), bvh.PrimitiveOrder().end());

    const Vector3f shift{50.0f, -25.0f, 10.0f};
    for (AABB &box : boxes)
    {
        box.min_ = box.min_ + shift;
        box.max_ = box.max_ + shift;
    }
    ASSERT_TRUE(bvh.Refit(boxes));

    ASSERT_EQ(bvh.PrimitiveOrder().size(), order_before.size());
    for (std::size_t i = 0; i < order_before.size(); ++i)
    {
        EXPECT_EQ(bvh.PrimitiveOrder()[i], order_before[i]);
    }

    for (int i = 0; i < 200; ++i)
    {
        const Ray ray = MakeRandomRay(random);
        const std::optional<LinearBVHRayHit> expected = BruteForceNearest(ray, boxes, kInfinity);
        const std::optional<LinearBVHRayHit> actual = bvh.IntersectRay(ray);
        ASSERT_EQ(actual.has_value(), expected.has_value());
        if (expected.has_value())
        {
            EXPECT_FLOAT_EQ(actual->distance, expected->distance);
        }
    }

    std::vector<uint32_t> actual_overlaps;
    bvh.QueryOverlap(AABB{{-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f}}, actual_overlaps);
    std::sort(actual_overlaps.begin(), actual_overlaps.end());
    EXPECT_EQ(actual_overlaps.size(), boxes.size());
}

TEST(LinearBVHTest, RefitRejectsACountMismatchAndLeavesTheTreeUntouched)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(50);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    const std::vector<LinearBVHNode> before(bvh.Nodes().begin(), bvh.Nodes().end());
    EXPECT_FALSE(bvh.Refit(std::span<const AABB>(boxes).first(10)));

    ASSERT_EQ(bvh.Nodes().size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_EQ(bvh.Nodes()[i].bounds, before[i].bounds);
    }
}

TEST(LinearBVHTest, RepairsMalformedPrimitivesWithoutDroppingThem)
{
    std::vector<AABB> boxes = MakeRandomBoxes(40);
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // Swapped: recoverable by swapping, so the box is genuinely searchable again.
    boxes.push_back(AABB{{5.0f, 5.0f, -5.0f}, {-5.0f, -5.0f, 5.0f}});
    // Non-finite: not placeable, collapsed to a point.
    boxes.push_back(AABB{{nan, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    boxes.push_back(AABB{{0.0f, 0.0f, 0.0f}, {kInfinity, 1.0f, 1.0f}});

    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    EXPECT_EQ(bvh.PrimitiveCount(), boxes.size());
    EXPECT_EQ(bvh.DegeneratePrimitiveCount(), 3u);

    const std::span<const uint32_t> degenerate = bvh.DegeneratePrimitives();
    ASSERT_EQ(degenerate.size(), 3u);
    EXPECT_EQ(degenerate[0], 40u);
    EXPECT_EQ(degenerate[1], 41u);
    EXPECT_EQ(degenerate[2], 42u);

    // Nothing is dropped and every stored box is well-formed, so traversal cannot
    // meet a NaN and ExpandToInclude cannot silently swallow a corner.
    std::vector<uint32_t> order(bvh.PrimitiveOrder().begin(), bvh.PrimitiveOrder().end());
    std::sort(order.begin(), order.end());
    ASSERT_EQ(order.size(), boxes.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(order[i], i);
    }
    for (const AABB &stored : bvh.PrimitiveBounds())
    {
        ExpectFiniteValidBounds(stored);
    }
    for (const LinearBVHNode &node : bvh.Nodes())
    {
        ExpectFiniteValidBounds(node.bounds);
    }

    // The swapped box was repaired into the extent it clearly meant, so it is
    // reachable by a ray through its centre.
    std::vector<uint32_t> overlaps;
    bvh.QueryOverlap(AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}, overlaps);
    EXPECT_NE(std::find(overlaps.begin(), overlaps.end(), 40u), overlaps.end());
}

TEST(LinearBVHTest, HealthyInputReportsNoDegeneratePrimitives)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(200);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    EXPECT_EQ(bvh.DegeneratePrimitiveCount(), 0u);
    EXPECT_TRUE(bvh.DegeneratePrimitives().empty());
}

TEST(LinearBVHTest, BuildIsDeterministicForIdenticalInput)
{
    std::vector<AABB> boxes = MakeRandomBoxes(400);
    // Duplicated boxes create equal-cost split ties on purpose.
    const std::size_t unique_count = boxes.size();
    boxes.reserve(unique_count * 2);
    for (std::size_t i = 0; i < unique_count; ++i)
    {
        const AABB duplicate = boxes[i];
        boxes.push_back(duplicate);
    }

    LinearBVH first;
    LinearBVH second;
    ASSERT_TRUE(first.Build(boxes));
    ASSERT_TRUE(second.Build(boxes));

    ASSERT_EQ(first.Nodes().size(), second.Nodes().size());
    for (std::size_t i = 0; i < first.Nodes().size(); ++i)
    {
        EXPECT_EQ(first.Nodes()[i].bounds, second.Nodes()[i].bounds);
        EXPECT_EQ(first.Nodes()[i].first, second.Nodes()[i].first);
        EXPECT_EQ(first.Nodes()[i].count, second.Nodes()[i].count);
        EXPECT_EQ(first.Nodes()[i].right, second.Nodes()[i].right);
    }

    ASSERT_EQ(first.PrimitiveOrder().size(), second.PrimitiveOrder().size());
    for (std::size_t i = 0; i < first.PrimitiveOrder().size(); ++i)
    {
        EXPECT_EQ(first.PrimitiveOrder()[i], second.PrimitiveOrder()[i]);
    }
}

TEST(LinearBVHTest, ClearResetsToEmptyStateAndRebuildSucceeds)
{
    const std::vector<AABB> boxes = MakeRandomBoxes(200);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    bvh.Clear();
    EXPECT_TRUE(bvh.IsEmpty());
    EXPECT_EQ(bvh.NodeCount(), 0u);
    EXPECT_EQ(bvh.PrimitiveCount(), 0u);
    EXPECT_FALSE(bvh.Bounds().IsValid());
    EXPECT_EQ(bvh.DegeneratePrimitiveCount(), 0u);
    EXPECT_FALSE(bvh.IntersectRay(Ray{{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}}).has_value());

    ASSERT_TRUE(bvh.Build(boxes));
    ASSERT_FALSE(bvh.IsEmpty());
    EXPECT_EQ(bvh.PrimitiveCount(), boxes.size());

    std::mt19937 random = MakeRandom();
    for (int i = 0; i < 100; ++i)
    {
        const Ray ray = MakeRandomRay(random);
        const std::optional<LinearBVHRayHit> expected = BruteForceNearest(ray, boxes, kInfinity);
        const std::optional<LinearBVHRayHit> actual = bvh.IntersectRay(ray);
        ASSERT_EQ(actual.has_value(), expected.has_value());
        if (expected.has_value())
        {
            EXPECT_FLOAT_EQ(actual->distance, expected->distance);
        }
    }
}

namespace
{
    // A conservative region test in the shape a frustum caller supplies: it
    // rejects a box only when the whole box is on the far side of the plane, so
    // rejecting a node implies rejecting everything inside it. QueryFiltered's
    // contract is exactly this, and this test is what holds it.
    struct HalfSpace
    {
        Vector3f normal{1.0f, 0.0f, 0.0f};
        float offset = 0.0f;

        bool operator()(const AABB &bounds) const
        {
            const Vector3f positive_vertex{
                normal.x_ >= 0.0f ? bounds.max_.x_ : bounds.min_.x_,
                normal.y_ >= 0.0f ? bounds.max_.y_ : bounds.min_.y_,
                normal.z_ >= 0.0f ? bounds.max_.z_ : bounds.min_.z_,
            };
            return normal.DotProduct(positive_vertex) >= offset;
        }
    };
}

TEST(LinearBVHTest, QueryFilteredMatchesBruteForceForAConservativePredicate)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 500);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    std::uniform_real_distribution<float> component(-1.0f, 1.0f);
    std::uniform_real_distribution<float> offset(-25.0f, 25.0f);
    for (int i = 0; i < 60; ++i)
    {
        HalfSpace region;
        region.normal = Vector3f{component(random), component(random), component(random)}
                            .GetSafetyNormalize();
        region.offset = offset(random);

        std::vector<uint32_t> actual;
        bvh.QueryFiltered(region, actual);
        std::sort(actual.begin(), actual.end());

        // The same predicate applied to every box is the definition of the
        // answer; pruning is only allowed to skip work, never results.
        std::vector<uint32_t> expected;
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            if (region(boxes[index]))
            {
                expected.push_back(static_cast<uint32_t>(index));
            }
        }

        EXPECT_EQ(actual, expected);
    }
}

TEST(LinearBVHTest, QueryFilteredHandlesTheExtremePredicates)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 200);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    // Accepting nothing must prune at the root, not walk the tree.
    std::vector<uint32_t> rejects_all;
    bvh.QueryFiltered([](const AABB &) { return false; }, rejects_all);
    EXPECT_TRUE(rejects_all.empty());

    // Accepting everything must report every primitive, exactly once each.
    std::vector<uint32_t> accepts_all;
    bvh.QueryFiltered([](const AABB &) { return true; }, accepts_all);
    std::sort(accepts_all.begin(), accepts_all.end());
    ASSERT_EQ(accepts_all.size(), boxes.size());
    for (std::size_t index = 0; index < accepts_all.size(); ++index)
    {
        EXPECT_EQ(accepts_all[index], index);
    }
}

TEST(LinearBVHTest, QueryFilteredOnAnEmptyTreeReportsNothing)
{
    const LinearBVH bvh;
    std::vector<uint32_t> hits;

    bvh.QueryFiltered([](const AABB &) { return true; }, hits);
    EXPECT_TRUE(hits.empty());
}

TEST(LinearBVHTest, QueryFilteredAppendsToTheOutput)
{
    std::mt19937 random = MakeRandom();
    const std::vector<AABB> boxes = MakeRandomBoxes(random, 50);
    LinearBVH bvh;
    ASSERT_TRUE(bvh.Build(boxes));

    std::vector<uint32_t> hits{7u, 8u};
    bvh.QueryFiltered([](const AABB &) { return true; }, hits);

    ASSERT_EQ(hits.size(), boxes.size() + 2);
    EXPECT_EQ(hits[0], 7u);
    EXPECT_EQ(hits[1], 8u);
}
