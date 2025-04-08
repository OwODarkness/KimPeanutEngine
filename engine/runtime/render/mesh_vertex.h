#ifndef KPENGINE_RUNTIME_MESH_VERTEX_H
#define KPENGINE_RUNTIME_MESH_VERTEX_H

#include "runtime/core/math/math_header.h"

namespace kpengine
{
struct MeshVertex
{
    Vector3f position;
    Vector3f normal;
    Vector3f tangent;
    Vector2f tex_coord;
    Vector4f color;
};
}
#endif