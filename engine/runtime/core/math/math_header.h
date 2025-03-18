#ifndef KPENGINE_RUNTIME_MATH_HEADER_H
#define KPENGINE_RUNTIME_MATH_HEADER_H

#include "math.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

namespace kpengine{
    using Vector2f = math::Vector2<float>;
    using Vector2d = math::Vector2<double>;
    using Vector3f = math::Vector3<float>;
    using Vector3d = math::Vector3<double>;

    using Vector4f = math::Vector4<float>;
    using Vector4d = math::Vector4<double>;
}

#endif