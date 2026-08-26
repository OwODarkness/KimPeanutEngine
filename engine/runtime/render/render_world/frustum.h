#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_FRUSTUM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_FRUSTUM_H

#include <array>
#include <utility>

#include "math/math_header.h"
#include "spatial/aabb.h"

namespace kpengine::render
{
    // CPU-side view frustum for render-policy visibility. It contains no RHI
    // state and uses the non-transposed Matrix4f convention used by math code.
    class Frustum
    {
    public:
        static Frustum FromViewProjection(const Matrix4f &view_projection);

        // Returns false only when an AABB is definitely outside a plane. Invalid
        // bounds stay visible so bad producer data cannot make geometry disappear.
        bool Intersects(const spatial::AABB &bounds) const;

    private:
        struct Plane
        {
            Vector3f normal{};
            float distance = 0.0f;
        };

        explicit Frustum(std::array<Plane, 6> planes) : planes_(std::move(planes)) {}

        std::array<Plane, 6> planes_;
    };
}

#endif
