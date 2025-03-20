#ifndef KPENGINE_RUNTIME_MATH_HEADER_H
#define KPENGINE_RUNTIME_MATH_HEADER_H

#include "math.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "matrix3.h"
#include "matrix4.h"
namespace kpengine{
    using Vector2f = math::Vector2<float>;
    using Vector2d = math::Vector2<double>;
    using Vector3f = math::Vector3<float>;
    using Vector3d = math::Vector3<double>;
    using Vector4f = math::Vector4<float>;
    using Vector4d = math::Vector4<double>;
    using Matrix3f = math::Matrix3<float>;
    using Matrix3d = math::Matrix3<double>;
    using Matrix4f = math::Matrix4<float>;
    using Matrix4d = math::Matrix4<double>;
}

#endif