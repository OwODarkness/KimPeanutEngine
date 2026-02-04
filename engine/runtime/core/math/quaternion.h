#ifndef KPENGINE_RUNTIME_MATH_QUATERNION_H
#define KPENGINE_RUNTIME_MATH_QUATERNION_H

#include <cmath>

#include "math.h"

namespace kpengine::math
{
    template <typename T>
    class Rotator;
    template <typename T>
    class Vector3;

    template <typename T>
    class Quaternion
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Quaternion();
        Quaternion(T w, T x, T y, T z);
        Quaternion(const Quaternion &rhs);

        Quaternion operator*(const Quaternion &rhs) const;
        Quaternion &operator=(const Quaternion &rhs);

        void Normalize();
        Quaternion GetNormailizedQuat() const;
        Rotator<T> ToRotator() const;
        Quaternion Conjugate() const;
        Vector3<T> RotateVector(const Vector3<T> &v) const;

        static Quaternion FromAxisAngle(const Vector3<T> &axis, T angle);

    public:
        T w_;
        T x_;
        T y_;
        T z_;
    };

}

#endif // KPENGINE_RUNTIME_MATH_QUATERNION_H