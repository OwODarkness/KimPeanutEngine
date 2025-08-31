#ifndef KPENGINE_RUNTIME_GRAPHICS_VERTEX_H
#define KPENGINE_RUNTIME_GRAPHICS_VERTEX_H

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
    };
}

#endif