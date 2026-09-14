#include "spatial/linear_bvh.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kpengine::spatial
{
    namespace
    {
        // Mirrors the "no bounds yet" seed in TransformAABB, so an empty tree
        // reports invalid rather than a plausible-looking point at the origin.
        AABB EmptyBounds() noexcept
        {
            const float maximum = std::numeric_limits<float>::max();
            return AABB{{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
        }

        Vector3f MinComponents(const Vector3f &lhs, const Vector3f &rhs) noexcept
        {
            return Vector3f{std::min(lhs.x_, rhs.x_), std::min(lhs.y_, rhs.y_),
                            std::min(lhs.z_, rhs.z_)};
        }

        Vector3f MaxComponents(const Vector3f &lhs, const Vector3f &rhs) noexcept
        {
            return Vector3f{std::max(lhs.x_, rhs.x_), std::max(lhs.y_, rhs.y_),
                            std::max(lhs.z_, rhs.z_)};
        }

        void ExpandBounds(AABB &target, const AABB &other) noexcept
        {
            target.min_ = MinComponents(target.min_, other.min_);
            target.max_ = MaxComponents(target.max_, other.max_);
        }

        Vector3f Centroid(const AABB &bounds) noexcept
        {
            return (bounds.min_ + bounds.max_) * 0.5f;
        }

        AABB UnionBounds(const AABB &lhs, const AABB &rhs) noexcept
        {
            return AABB{MinComponents(lhs.min_, rhs.min_), MaxComponents(lhs.max_, rhs.max_)};
        }

        // Inclusive on touching faces, matching the conservative intent of culling.
        bool Overlaps(const AABB &lhs, const AABB &rhs) noexcept
        {
            return lhs.min_.x_ <= rhs.max_.x_ && lhs.max_.x_ >= rhs.min_.x_ &&
                   lhs.min_.y_ <= rhs.max_.y_ && lhs.max_.y_ >= rhs.min_.y_ &&
                   lhs.min_.z_ <= rhs.max_.z_ && lhs.max_.z_ >= rhs.min_.z_;
        }

        // Repairs a box into finite, ordered bounds and reports whether it had to.
        //
        // Storing the result rather than the raw input matters for more than
        // tidiness: AABB::ExpandToInclude uses std::min and std::max with the
        // incoming point as the *second* argument, so a NaN corner is silently
        // ignored and the primitive would vanish from its node's bounds with no
        // trace. Sanitizing here is what keeps that unreachable.
        bool SanitizeBounds(const AABB &source, AABB &out) noexcept
        {
            bool repaired = false;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                float low = source.min_[axis];
                float high = source.max_[axis];
                if (!std::isfinite(low))
                {
                    low = std::isfinite(high) ? high : 0.0f;
                    repaired = true;
                }
                if (!std::isfinite(high))
                {
                    high = low;
                    repaired = true;
                }
                if (low > high)
                {
                    // An inverted pair is most likely a producer writing the
                    // arguments the wrong way round, and swapping recovers the
                    // intended extent exactly. Collapsing to a point would discard
                    // a box that was only mis-ordered.
                    std::swap(low, high);
                    repaired = true;
                }
                out.min_[axis] = low;
                out.max_[axis] = high;
            }
            return repaired;
        }

        // Surface areas drive the SAH cost, so a non-finite box must not
        // contribute one. Sanitized bounds are always finite, so this only guards
        // the sentinel from EmptyBounds.
        float SurfaceArea(const AABB &bounds) noexcept
        {
            if (!std::isfinite(bounds.min_.x_) || !std::isfinite(bounds.min_.y_) ||
                !std::isfinite(bounds.min_.z_) || !std::isfinite(bounds.max_.x_) ||
                !std::isfinite(bounds.max_.y_) || !std::isfinite(bounds.max_.z_))
            {
                return 0.0f;
            }
            const float dx = bounds.max_.x_ - bounds.min_.x_;
            const float dy = bounds.max_.y_ - bounds.min_.y_;
            const float dz = bounds.max_.z_ - bounds.min_.z_;
            return 2.0f * (dx * dy + dy * dz + dz * dx);
        }
    }

    LinearBVH::LinearBVH() : bounds_(EmptyBounds())
    {
    }

    void LinearBVH::Sanitize(std::span<const AABB> primitives)
    {
        primitive_bounds_.resize(primitives.size());
        degenerates_.clear();
        for (std::size_t i = 0; i < primitives.size(); ++i)
        {
            AABB sanitized{};
            if (SanitizeBounds(primitives[i], sanitized))
            {
                degenerates_.push_back(static_cast<uint32_t>(i));
            }
            primitive_bounds_[i] = sanitized;
        }
    }

    bool LinearBVH::Build(std::span<const AABB> primitives, const LinearBVHBuildOptions &options)
    {
        Clear();
        if (options.max_leaf_size == 0 || options.max_depth == 0 || options.bin_count < 2)
        {
            return false;
        }
        max_depth_ = std::min(options.max_depth, kLinearBVHMaxDepth);

        if (primitives.empty())
        {
            return true;
        }

        Sanitize(primitives);

        const std::size_t count = primitives.size();
        order_.resize(count);
        std::vector<Vector3f> centroids(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            centroids[i] = Centroid(primitive_bounds_[i]);
            order_[i] = static_cast<uint32_t>(i);
        }

        nodes_.reserve(count);
        nodes_.push_back(LinearBVHNode{});
        BuildNode(0, 0, static_cast<uint32_t>(count), 0, options, centroids);
        bounds_ = nodes_.front().bounds;
        return true;
    }

    void LinearBVH::BuildNode(uint32_t index, uint32_t begin, uint32_t count, uint32_t depth,
                        const LinearBVHBuildOptions &options, std::span<const Vector3f> centroids)
    {
        AABB node_bounds = primitive_bounds_[order_[begin]];
        for (uint32_t i = 1; i < count; ++i)
        {
            ExpandBounds(node_bounds, primitive_bounds_[order_[begin + i]]);
        }
        // Never hold a LinearBVHNode reference across a push_back: nodes_ reallocates.
        nodes_[index].bounds = node_bounds;

        // The depth cap is a terminator, not a quality switch: it keeps recursion
        // depth bounded for adversarial input, and it can produce an oversized leaf.
        if (count <= options.max_leaf_size || depth >= max_depth_)
        {
            nodes_[index].first = begin;
            nodes_[index].count = count;
            nodes_[index].right = 0;
            return;
        }

        const uint32_t middle = PartitionRange(begin, count, options, centroids);

        // Children are created before recursion, which is what makes a child's
        // index exceed its parent's.
        const uint32_t left_index = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(LinearBVHNode{});
        nodes_.push_back(LinearBVHNode{});
        nodes_[index].first = left_index;
        nodes_[index].right = left_index + 1;
        nodes_[index].count = 0;

        BuildNode(left_index, begin, middle - begin, depth + 1, options, centroids);
        BuildNode(left_index + 1, middle, begin + count - middle, depth + 1, options, centroids);
    }

    // Reorders order_[begin, begin + count) for the chosen split and returns the
    // first index of the right half. The result is always strictly inside the
    // range, so the caller's recursion always makes progress: when no surface area
    // split is possible (every centroid identical, or a zero-area parent) the
    // range is halved by position instead, which needs no reordering at all.
    uint32_t LinearBVH::PartitionRange(uint32_t begin, uint32_t count,
                                 const LinearBVHBuildOptions &options,
                                 std::span<const Vector3f> centroids)
    {
        const uint32_t fallback = begin + count / 2;

        AABB centroid_bounds = EmptyBounds();
        for (uint32_t i = 0; i < count; ++i)
        {
            const Vector3f &centroid = centroids[order_[begin + i]];
            centroid_bounds.min_ = MinComponents(centroid_bounds.min_, centroid);
            centroid_bounds.max_ = MaxComponents(centroid_bounds.max_, centroid);
        }

        const Vector3f extent = centroid_bounds.max_ - centroid_bounds.min_;
        std::size_t axis = 0;
        if (extent.y_ > extent.x_)
        {
            axis = 1;
        }
        if (extent.z_ > extent[axis])
        {
            axis = 2;
        }
        if (!std::isfinite(extent[axis]) || extent[axis] <= 0.0f)
        {
            return fallback;
        }

        const uint32_t bin_count = std::min(options.bin_count, kLinearBVHMaxBinCount);
        const float inverse_extent = 1.0f / extent[axis];
        const float origin = centroid_bounds.min_[axis];

        struct Bin
        {
            AABB bounds{};
            uint32_t count = 0;
        };
        std::array<Bin, kLinearBVHMaxBinCount> bins{};
        // Evaluated again when partitioning. That is safe only because this is a
        // single pure function of the same values, so the two passes cannot
        // disagree about which primitive belongs to which bin.
        const auto bin_of = [&](uint32_t primitive) {
            const float offset = (centroids[primitive][axis] - origin) * inverse_extent;
            if (!(offset > 0.0f))
            {
                return uint32_t{0};
            }
            const uint32_t bin = static_cast<uint32_t>(offset * static_cast<float>(bin_count));
            return bin < bin_count ? bin : bin_count - 1;
        };

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t primitive = order_[begin + i];
            const uint32_t bin = bin_of(primitive);
            if (bins[bin].count == 0)
            {
                bins[bin].bounds = primitive_bounds_[primitive];
            }
            else
            {
                ExpandBounds(bins[bin].bounds, primitive_bounds_[primitive]);
            }
            ++bins[bin].count;
        }

        std::array<AABB, kLinearBVHMaxBinCount> left_bounds{};
        std::array<uint32_t, kLinearBVHMaxBinCount> left_counts{};
        AABB running = EmptyBounds();
        uint32_t running_count = 0;
        for (uint32_t bin = 0; bin < bin_count; ++bin)
        {
            if (bins[bin].count != 0)
            {
                if (running_count == 0)
                {
                    running = bins[bin].bounds;
                }
                else
                {
                    ExpandBounds(running, bins[bin].bounds);
                }
                running_count += bins[bin].count;
            }
            left_bounds[bin] = running;
            left_counts[bin] = running_count;
        }

        AABB suffix = EmptyBounds();
        uint32_t suffix_count = 0;
        float best_cost = std::numeric_limits<float>::infinity();
        uint32_t best_bin = kLinearBVHInvalidIndex;
        // Strictly-less keeps the first minimum, and the sweep direction is fixed,
        // so equal-cost ties resolve the same way on every run and every compiler.
        for (uint32_t bin = bin_count - 1; bin > 0; --bin)
        {
            if (bins[bin].count != 0)
            {
                if (suffix_count == 0)
                {
                    suffix = bins[bin].bounds;
                }
                else
                {
                    ExpandBounds(suffix, bins[bin].bounds);
                }
                suffix_count += bins[bin].count;
            }

            const uint32_t left_count = left_counts[bin - 1];
            if (left_count == 0 || suffix_count == 0)
            {
                continue;
            }

            const float cost = SurfaceArea(left_bounds[bin - 1]) * static_cast<float>(left_count) +
                               SurfaceArea(suffix) * static_cast<float>(suffix_count);
            if (cost < best_cost)
            {
                best_cost = cost;
                best_bin = bin - 1;
            }
        }

        if (best_bin == kLinearBVHInvalidIndex)
        {
            return fallback;
        }

        // stable_partition, not partition: it preserves relative order, so an
        // identical input always yields an identical tree. The standard fixes only
        // the partitioning property for the unstable form, which would make
        // PrimitiveOrder() and leaf membership differ between MSVC and Clang.
        std::stable_partition(order_.begin() + begin, order_.begin() + begin + count,
                              [&](uint32_t primitive) { return bin_of(primitive) <= best_bin; });
        return begin + left_counts[best_bin];
    }

    bool LinearBVH::Refit(std::span<const AABB> primitives)
    {
        if (nodes_.empty() || primitives.size() != primitive_bounds_.size())
        {
            return false;
        }

        // Re-run, because the repaired set is a property of the current input: a
        // primitive that was malformed at build time and is healthy now must leave
        // the reported set, or DegeneratePrimitives stops describing reality.
        Sanitize(primitives);

        // Children always outrank their parent, so descending index order is a
        // valid bottom-up sweep with no stack.
        for (std::size_t i = nodes_.size(); i-- > 0;)
        {
            LinearBVHNode &node = nodes_[i];
            if (node.IsLeaf())
            {
                AABB node_bounds = primitive_bounds_[order_[node.first]];
                for (uint32_t k = 1; k < node.count; ++k)
                {
                    ExpandBounds(node_bounds, primitive_bounds_[order_[node.first + k]]);
                }
                node.bounds = node_bounds;
            }
            else
            {
                // Assign, never accumulate: unioning into the stale bounds would
                // make the tree grow on every refit.
                node.bounds = UnionBounds(nodes_[node.first].bounds, nodes_[node.right].bounds);
            }
        }

        bounds_ = nodes_.front().bounds;
        return true;
    }

    void LinearBVH::Clear() noexcept
    {
        nodes_.clear();
        order_.clear();
        primitive_bounds_.clear();
        degenerates_.clear();
        bounds_ = EmptyBounds();
        max_depth_ = kLinearBVHMaxDepth;
    }

    std::optional<LinearBVHRayHit> LinearBVH::IntersectRay(const Ray &ray, float max_distance) const
    {
        if (nodes_.empty() || !ray.IsValid() || !(max_distance >= 0.0f))
        {
            return std::nullopt;
        }

        const Vector3f inverse_direction{1.0f / ray.direction.x_, 1.0f / ray.direction.y_,
                                         1.0f / ray.direction.z_};

        LinearBVHRayHit best{};
        best.distance = max_distance;
        std::array<uint32_t, kTraversalStackCapacity> stack{};
        std::size_t stack_size = 0;
        stack[stack_size++] = 0;

        while (stack_size > 0)
        {
            const uint32_t index = stack[--stack_size];
            const LinearBVHNode &node = nodes_[index];

            // A miss returns infinity, which this comparison rejects, so the test
            // covers both the miss case and nodes beyond the nearest hit.
            if (!(detail::IntersectRayBounds(node.bounds, ray.origin, inverse_direction,
                                             best.distance) < best.distance))
            {
                continue;
            }

            if (node.IsLeaf())
            {
                for (uint32_t k = 0; k < node.count; ++k)
                {
                    const uint32_t primitive = order_[node.first + k];
                    // Reuses the validated reference path. Its kParallelEpsilon
                    // branch treats a near-zero direction component as exactly
                    // parallel, which only ever widens what it accepts, and the
                    // node test above cannot reject those, so this stays
                    // conservative: the primitive is reached and decides.
                    const std::optional<float> distance =
                        IntersectRayAABB(ray, primitive_bounds_[primitive]);
                    if (distance.has_value() && *distance < best.distance)
                    {
                        best.primitive = primitive;
                        best.distance = *distance;
                    }
                }
                continue;
            }

            const float left_distance = detail::IntersectRayBounds(
                nodes_[node.first].bounds, ray.origin, inverse_direction, best.distance);
            const float right_distance = detail::IntersectRayBounds(
                nodes_[node.right].bounds, ray.origin, inverse_direction, best.distance);

            // The farther child is pushed first so the nearer one is popped next;
            // that ordering is what lets the nearest hit prune the rest.
            if (left_distance <= right_distance)
            {
                if (right_distance < best.distance)
                {
                    assert(stack_size < kTraversalStackCapacity);
                    stack[stack_size++] = node.right;
                }
                if (left_distance < best.distance)
                {
                    assert(stack_size < kTraversalStackCapacity);
                    stack[stack_size++] = node.first;
                }
            }
            else
            {
                if (left_distance < best.distance)
                {
                    assert(stack_size < kTraversalStackCapacity);
                    stack[stack_size++] = node.first;
                }
                if (right_distance < best.distance)
                {
                    assert(stack_size < kTraversalStackCapacity);
                    stack[stack_size++] = node.right;
                }
            }
        }

        if (best.primitive == kLinearBVHInvalidIndex)
        {
            return std::nullopt;
        }
        return best;
    }

    void LinearBVH::QueryOverlap(const AABB &query, std::vector<uint32_t> &out) const
    {
        if (nodes_.empty() || !query.IsValid())
        {
            return;
        }

        std::array<uint32_t, kTraversalStackCapacity> stack{};
        std::size_t stack_size = 0;
        stack[stack_size++] = 0;

        while (stack_size > 0)
        {
            const LinearBVHNode &node = nodes_[stack[--stack_size]];
            if (!Overlaps(node.bounds, query))
            {
                continue;
            }

            if (node.IsLeaf())
            {
                for (uint32_t k = 0; k < node.count; ++k)
                {
                    const uint32_t primitive = order_[node.first + k];
                    if (Overlaps(primitive_bounds_[primitive], query))
                    {
                        out.push_back(primitive);
                    }
                }
                continue;
            }

            assert(stack_size + 2 <= kTraversalStackCapacity);
            stack[stack_size++] = node.first;
            stack[stack_size++] = node.right;
        }
    }
}
