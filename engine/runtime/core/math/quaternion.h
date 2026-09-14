#ifndef KPENGINE_RUNTIME_MATH_QUATERNION_H
#define KPENGINE_RUNTIME_MATH_QUATERNION_H

#include <concepts>
#include <cmath>

#include "math.h"

namespace kpengine::math
{
    template <std::floating_point T>
    class Rotator;
    template <std::floating_point T>
    class Vector3;

    template <std::floating_point T>
    class Quaternion
    {
    public:
        Quaternion();
        Quaternion(T w, T x, T y, T z);
        Quaternion(const Quaternion &rhs);

        Quaternion operator*(const Quaternion &rhs) const;
        Quaternion &operator=(const Quaternion &rhs);

        void Normalize();
        Quaternion GetNormalizedQuat() const;
        // Compatibility spelling retained for existing callers.
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
#include "quaternion.tpp"
#endif // KPENGINE_RUNTIME_MATH_QUATERNION_H
