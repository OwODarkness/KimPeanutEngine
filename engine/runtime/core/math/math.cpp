#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"
#include "rotator.h"
#include "transform.h"
#include "matrix3.h"
#include "matrix4.h"


#include "vector2.tpp"
#include "vector3.tpp"
#include "vector4.tpp"
#include "quaternion.tpp"
#include "rotator.tpp"
#include "transform.tpp"
#include "matrix3.tpp"
#include "matrix4.tpp"

namespace kpengine::math
{
    template class Vector2<float>;
    template class Vector2<double>;

    template class Vector3<float>;
    template class Vector3<double>;

    template class Vector4<float>;
    template class Vector4<double>;

    template class Quaternion<float>;
    template class Quaternion<double>;

    template class Rotator<float>;
    template class Rotator<double>;

    template class Transform<float>;
    template class Transform<double>;

    template class Matrix3<float>;
    template class Matrix3<double>;

    template class Matrix4<float>;
    template class Matrix4<double>;
}
