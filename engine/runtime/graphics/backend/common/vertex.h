#ifndef KPENGINE_RUNTIME_GRAPHICS_VERTEX_H
#define KPENGINE_RUNTIME_GRAPHICS_VERTEX_H

#include <functional>
#include "math/math_header.h"
namespace kpengine::graphics
{
    struct Vertex
    {
        Vector3f position;
        Vector3f normal;
        Vector2f tex_coord;
        Vector3f tangent;
        Vector3f bitangent;

        bool operator==(const Vertex &other) const noexcept
        {
            return position == other.position &&
                   normal == other.normal &&
                   tex_coord == other.tex_coord &&
                   tangent == other.tangent &&
                   bitangent == other.bitangent;
        }
    };

    struct VertexHash
    {
        std::size_t operator()(const Vertex &v) const noexcept
        {
            auto hashCombine = [](std::size_t seed, std::size_t h)
            {
                // boost::hash_combine style
                return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
            };

            std::size_t h = 0;
            h = hashCombine(h, std::hash<float>()(v.position.x_));
            h = hashCombine(h, std::hash<float>()(v.position.y_));
            h = hashCombine(h, std::hash<float>()(v.position.z_));

            h = hashCombine(h, std::hash<float>()(v.normal.x_));
            h = hashCombine(h, std::hash<float>()(v.normal.y_));
            h = hashCombine(h, std::hash<float>()(v.normal.z_));

            h = hashCombine(h, std::hash<float>()(v.tex_coord.x_));
            h = hashCombine(h, std::hash<float>()(v.tex_coord.y_));

            h = hashCombine(h, std::hash<float>()(v.tangent.x_));
            h = hashCombine(h, std::hash<float>()(v.tangent.y_));
            h = hashCombine(h, std::hash<float>()(v.tangent.z_));

            h = hashCombine(h, std::hash<float>()(v.bitangent.x_));
            h = hashCombine(h, std::hash<float>()(v.bitangent.y_));
            h = hashCombine(h, std::hash<float>()(v.bitangent.z_));

            return h;
        }
    };
}

#endif