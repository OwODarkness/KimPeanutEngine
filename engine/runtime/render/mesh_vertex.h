#ifndef KPENGINE_RUNTIME_MESH_VERTEX_H
#define KPENGINE_RUNTIME_MESH_VERTEX_H

#include "runtime/core/math/math_header.h"

namespace kpengine{

struct MeshVertex{
    Vector3f position;
    Vector3f normal;
    Vector3f tangent;
    Vector4f color;
    Vector2f tex_coord;
};
}
#endif