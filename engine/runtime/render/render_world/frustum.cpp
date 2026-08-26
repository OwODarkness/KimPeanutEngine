#include "render/render_world/frustum.h"

#include <cmath>

namespace kpengine::render
{
    namespace
    {
    }

    Frustum Frustum::FromViewProjection(const Matrix4f &view_projection)
    {
        const auto make_plane = [](float a, float b, float c, float d)
        {
            const float length = std::sqrt(a * a + b * b + c * c);
            if (length == 0.0f)
            {
                return Plane{};
            }
            return Plane{{a / length, b / length, c / length}, d / length};
        };

        // Matrix4f multiplies column vectors. Homogeneous clip planes are
        // therefore row3 +/- row0/row1/row2 for OpenGL-style [-w, w] depth.
        return Frustum({
            make_plane(view_projection[3][0] + view_projection[0][0],
                       view_projection[3][1] + view_projection[0][1],
                       view_projection[3][2] + view_projection[0][2],
                       view_projection[3][3] + view_projection[0][3]),
            make_plane(view_projection[3][0] - view_projection[0][0],
                       view_projection[3][1] - view_projection[0][1],
                       view_projection[3][2] - view_projection[0][2],
                       view_projection[3][3] - view_projection[0][3]),
            make_plane(view_projection[3][0] + view_projection[1][0],
                       view_projection[3][1] + view_projection[1][1],
                       view_projection[3][2] + view_projection[1][2],
                       view_projection[3][3] + view_projection[1][3]),
            make_plane(view_projection[3][0] - view_projection[1][0],
                       view_projection[3][1] - view_projection[1][1],
                       view_projection[3][2] - view_projection[1][2],
                       view_projection[3][3] - view_projection[1][3]),
            make_plane(view_projection[3][0] + view_projection[2][0],
                       view_projection[3][1] + view_projection[2][1],
                       view_projection[3][2] + view_projection[2][2],
                       view_projection[3][3] + view_projection[2][3]),
            make_plane(view_projection[3][0] - view_projection[2][0],
                       view_projection[3][1] - view_projection[2][1],
                       view_projection[3][2] - view_projection[2][2],
                       view_projection[3][3] - view_projection[2][3]),
        });
    }

    bool Frustum::Intersects(const spatial::AABB &bounds) const
    {
        if (!bounds.IsValid())
        {
            return true;
        }

        for (const Plane &plane : planes_)
        {
            const Vector3f positive_vertex{
                plane.normal.x_ >= 0.0f ? bounds.max_.x_ : bounds.min_.x_,
                plane.normal.y_ >= 0.0f ? bounds.max_.y_ : bounds.min_.y_,
                plane.normal.z_ >= 0.0f ? bounds.max_.z_ : bounds.min_.z_,
            };
            if (plane.normal.DotProduct(positive_vertex) + plane.distance < 0.0f)
            {
                return false;
            }
        }
        return true;
    }
}
