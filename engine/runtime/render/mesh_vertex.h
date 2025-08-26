#ifndef KPENGINE_RUNTIME_MESH_VERTEX_H
#define KPENGINE_RUNTIME_MESH_VERTEX_H

#include "math/math_header.h"

namespace kpengine
{
struct MeshVertex
{
    Vector3f position;
    Vector3f normal;
    Vector2f tex_coord;
    Vector3f tangent;
    Vector3f bitangent;
};
}
#endif