#ifndef KPENGINE_RUNTIME_CORE_SPATIAL_LINEAR_BVH_H
#define KPENGINE_RUNTIME_CORE_SPATIAL_LINEAR_BVH_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "spatial/aabb.h"
#include "spatial/ray.h"

namespace kpengine::spatial
{
    inline constexpr uint32_t kLinearBVHInvalidIndex = std::numeric_limits<uint32_t>::max();

    // Build depth is capped so traversal can use a fixed-size stack. Real trees
    // over scene-sized inputs are about log2(n / max_leaf_size) deep, so this only
    // bites on adversarial input.
    inline constexpr uint32_t kLinearBVHMaxDepth = 64;
    inline constexpr uint32_t kLinearBVHMaxBinCount = 32;

    struct LinearBVHBuildOptions
    {
        // A hard cap, not a quality hint: a leaf never exceeds this, so worst-case
        // traversal cost stays predictable. The SAH only chooses where to split.
        uint32_t max_leaf_size = 4;
        // Clamped to kLinearBVHMaxDepth, which is what keeps query stacks fixed-size.
        uint32_t max_depth = kLinearBVHMaxDepth;
        uint32_t bin_count = 12;
    };

    // Nodes are allocated depth-first, so a child always has a higher index than
    // its parent. Refit relies on that, and so does bottom-up traversal.
    struct LinearBVHNode
    {
        AABB bounds{};
        uint32_t first = 0; // internal: left child node; leaf: first entry in the order
        uint32_t count = 0; // 0 marks an internal node, otherwise the leaf's primitive count
        uint32_t right = 0; // internal: right child node; unused by leaves

        bool IsLeaf() const noexcept
        {
            return count != 0;
        }
    };

    struct LinearBVHRayHit
    {
        uint32_t primitive = kLinearBVHInvalidIndex;
        float distance = std::numeric_limits<float>::infinity();
    };

    namespace detail
    {
        // Header-resident only because the IntersectRayAll template member below
        // inlines it. Every other internal helper is local to linear_bvh.cpp.
        //
        // Entry distance for a precomputed reciprocal direction, or infinity on a
        // miss. The running value must stay the FIRST argument: a zero direction
        // component gives an infinite reciprocal, and an origin exactly on a slab
        // plane then yields 0 * inf = NaN, which std::min/std::max absorb by
        // returning that first argument. Writing this as
        // std::max(std::min(a, b), running) reintroduces the NaN.
        inline float IntersectRayBounds(const AABB &bounds, const Vector3f &origin,
                                        const Vector3f &inverse_direction,
                                        float max_distance) noexcept
        {
            float near_distance = 0.0f;
            float far_distance = max_distance;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const float first = (bounds.min_[axis] - origin[axis]) * inverse_direction[axis];
                const float second = (bounds.max_[axis] - origin[axis]) * inverse_direction[axis];
                near_distance = std::max(near_distance, std::min(first, second));
                far_distance = std::min(far_distance, std::max(first, second));
            }
            return near_distance <= far_distance ? near_distance
                                                 : std::numeric_limits<float>::infinity();
        }
    }

    // Bounding volume hierarchy over axis-aligned boxes. Primitives are addressed
    // by their position in the array passed to Build, so callers keep ownership of
    // their own objects and only this index permutation lives here.
    //
    // Build copies the boxes it is given; queries need no access to the caller's
    // array. Any box that was not already finite and ordered is repaired, and its
    // index is reported by DegeneratePrimitives. Repairing keeps traversal
    // well-formed but cannot make a box that was never placeable searchable, so
    // IntersectRay finds only primitives whose repaired box the ray really hits;
    // a caller that must keep such primitives visible regardless of geometry
    // handles them through DegeneratePrimitives. This is deliberately not the same
    // policy as Render's "malformed bounds remain visible" rule, which the render
    // caller applies itself.
    //
    // This class is not a template, so its implementation lives in
    // linear_bvh.cpp. Only IntersectRayAll is defined here, because a template
    // member cannot be.
    class LinearBVH
    {
    public:
        // Seed bounds_ as "no bounds", so Bounds() on an empty tree reports
        // invalid rather than a valid-looking degenerate point at the origin.
        LinearBVH();

        // Returns false only for options that cannot describe a tree; an empty
        // input is a successful build of an empty tree.
        bool Build(std::span<const AABB> primitives, const LinearBVHBuildOptions &options = {});
        // Re-supplies bounds for the same primitive count and order, keeping the
        // topology. Returns false when the count differs, leaving the tree
        // untouched. Topology is fixed, so a scene that drifts degrades toward a
        // linear scan; rebuild periodically rather than refitting forever.
        bool Refit(std::span<const AABB> primitives);
        void Clear() noexcept;

        bool IsEmpty() const noexcept
        {
            return nodes_.empty();
        }

        // Invalid while the tree is empty; use IsEmpty as the primary check.
        const AABB &Bounds() const noexcept
        {
            return bounds_;
        }

        uint32_t PrimitiveCount() const noexcept
        {
            return static_cast<uint32_t>(primitive_bounds_.size());
        }

        uint32_t NodeCount() const noexcept
        {
            return static_cast<uint32_t>(nodes_.size());
        }

        uint32_t DegeneratePrimitiveCount() const noexcept
        {
            return static_cast<uint32_t>(degenerates_.size());
        }

        // Indices of the primitives whose boxes had to be repaired, ascending.
        std::span<const uint32_t> DegeneratePrimitives() const noexcept
        {
            return degenerates_;
        }

        std::span<const LinearBVHNode> Nodes() const noexcept
        {
            return nodes_;
        }

        // A permutation, not a leaf grouping: each leaf occupies a contiguous
        // range and the ranges tile [0, PrimitiveCount()), but the order within a
        // leaf is an artifact of the partition.
        std::span<const uint32_t> PrimitiveOrder() const noexcept
        {
            return order_;
        }

        // The repaired boxes actually stored, which is what queries test against.
        std::span<const AABB> PrimitiveBounds() const noexcept
        {
            return primitive_bounds_;
        }

        std::optional<LinearBVHRayHit> IntersectRay(
            const Ray &ray,
            float max_distance = std::numeric_limits<float>::infinity()) const;

        // Visits every primitive whose stored box is hit. Order is unspecified.
        // This is the multi-hit shape a ray tracing query needs later.
        template <typename Visitor>
        void IntersectRayAll(const Ray &ray, float max_distance, Visitor &&visitor) const;

        // Appends every primitive whose stored box overlaps the query box. Each
        // primitive lives in exactly one leaf, so entries are unique.
        void QueryOverlap(const AABB &query, std::vector<uint32_t> &out) const;

        // Appends every primitive whose stored box satisfies `accept`, and is the
        // general form of QueryOverlap: a caller that needs a region test this
        // module has no vocabulary for -- a frustum, a light volume -- supplies it
        // here instead of teaching spatial about it.
        //
        // `accept` runs on node bounds to prune a whole subtree and on primitive
        // bounds to decide the result, so it must be a pure function of one AABB,
        // and conservative: rejecting a node must imply rejecting everything
        // inside it. Order is unspecified, and entries are unique because each
        // primitive lives in exactly one leaf.
        //
        // Note `accept` sees the repaired boxes, not the caller's originals. A
        // primitive with malformed input is indexed at a repaired position, so a
        // caller that must keep such primitives visible has to consult
        // DegeneratePrimitives and include those itself.
        template <typename Accept>
        void QueryFiltered(Accept &&accept, std::vector<uint32_t> &out) const;

    private:
        // One pending entry per level of the current path, so the depth cap bounds
        // this. Queries therefore never allocate.
        static constexpr uint32_t kTraversalStackCapacity = 2 * kLinearBVHMaxDepth + 2;

        void Sanitize(std::span<const AABB> primitives);

        uint32_t PartitionRange(uint32_t begin, uint32_t count, const LinearBVHBuildOptions &options,
                                std::span<const Vector3f> centroids);

        void BuildNode(uint32_t index, uint32_t begin, uint32_t count, uint32_t depth,
                       const LinearBVHBuildOptions &options, std::span<const Vector3f> centroids);

        std::vector<LinearBVHNode> nodes_;
        std::vector<uint32_t> order_;
        std::vector<AABB> primitive_bounds_;
        std::vector<uint32_t> degenerates_;
        AABB bounds_{};
        uint32_t max_depth_ = kLinearBVHMaxDepth;
    };

    template <typename Accept>
    void LinearBVH::QueryFiltered(Accept &&accept, std::vector<uint32_t> &out) const
    {
        if (nodes_.empty())
        {
            return;
        }

        std::array<uint32_t, kTraversalStackCapacity> stack{};
        std::size_t stack_size = 0;
        stack[stack_size++] = 0;

        while (stack_size > 0)
        {
            const LinearBVHNode &node = nodes_[stack[--stack_size]];
            if (!accept(node.bounds))
            {
                continue;
            }

            if (node.IsLeaf())
            {
                for (uint32_t k = 0; k < node.count; ++k)
                {
                    const uint32_t primitive = order_[node.first + k];
                    if (accept(primitive_bounds_[primitive]))
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

    template <typename Visitor>
    void LinearBVH::IntersectRayAll(const Ray &ray, float max_distance, Visitor &&visitor) const
    {
        if (nodes_.empty() || !ray.IsValid() || !(max_distance >= 0.0f))
        {
            return;
        }

        const Vector3f inverse_direction{1.0f / ray.direction.x_, 1.0f / ray.direction.y_,
                                         1.0f / ray.direction.z_};

        std::array<uint32_t, kTraversalStackCapacity> stack{};
        std::size_t stack_size = 0;
        stack[stack_size++] = 0;

        while (stack_size > 0)
        {
            const LinearBVHNode &node = nodes_[stack[--stack_size]];
            if (!(detail::IntersectRayBounds(node.bounds, ray.origin, inverse_direction,
                                             max_distance) < max_distance))
            {
                continue;
            }

            if (node.IsLeaf())
            {
                for (uint32_t k = 0; k < node.count; ++k)
                {
                    const uint32_t primitive = order_[node.first + k];
                    const std::optional<float> distance =
                        IntersectRayAABB(ray, primitive_bounds_[primitive]);
                    if (distance.has_value() && *distance < max_distance)
                    {
                        visitor(primitive, *distance);
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

#endif
