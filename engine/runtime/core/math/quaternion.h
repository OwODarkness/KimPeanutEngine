#ifndef KPENGINE_RUNTIME_MATH_QUATERNION_H
#define KPENGINE_RUNTIME_MATH_QUATERNION_H

#include <cmath>

#include "math.h"
#include "vector3.h"
#include "rotator.h"

namespace kpengine::math
{
    template <typename T>
    class Rotator;

    template <typename T>
    class Quaternion
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Quaternion() : w_(1), x_(0), y_(0), z_(0) {}
        Quaternion(T w, T x, T y, T z) : w_(w), x_(x), y_(y), z_(z) {}
        Quaternion(const Quaternion &rhs) : w_(rhs.w_), x_(rhs.x_), y_(rhs.y_), z_(rhs.z_) {}

        static Quaternion FromAxisAngle(const Vector3<T> &axis, T angle)
        {
            T half_angle = angle * T(0.5);
            T s = std::sin(half_angle);

            return Quaternion(std::cos(half_angle), axis.x_ * s, axis.y_ * s, axis.z_ * s);
        }

        Quaternion operator*(const Quaternion &rhs) const
        {
            return {
                w_ * rhs.w_ - x_ * rhs.x_ - y_ * rhs.y_ - z_ * rhs.z_,
                w_ * rhs.x_ + x_ * rhs.w_ + y_ * rhs.z_ - z_ * rhs.y_,
                w_ * rhs.y_ - x_ * rhs.z_ + y_ * rhs.w_ + z_ * rhs.x_,
                w_ * rhs.z_ + x_ * rhs.y_ - y_ * rhs.x_ + z_ * rhs.w_};
        }

        Quaternion &operator=(const Quaternion &rhs)
        {
            w_ = rhs.w_;
            x_ = rhs.x_;
            y_ = rhs.y_;
            z_ = rhs.z_;
            return *this;
        }

        void Normalize();
        Quaternion GetNormailizedQuat() const;
        Rotator<T> ToRotator() const;
        Quaternion Conjugate() const;
        Vector3<T> RotateVector(const Vector3<T> &v) const;

    public:
        T w_;
        T x_;
        T y_;
        T z_;
    };

    template <typename T>
    void Quaternion<T>::Normalize()
    {
        T len = std::sqrt(w_ * w_ + x_ * x_ + y_ * y_ + z_ * z_);
        if (len == T(0))
            return;
        T coff = T(1) / len;
        w_ *= coff;
        x_ *= coff;
        y_ *= coff;
        z_ *= coff;
    }

    template<typename T>
    Quaternion<T> Quaternion<T>::GetNormailizedQuat() const
    {
        T len = std::sqrt(w_ * w_ + x_ * x_ + y_ * y_ + z_ * z_);
        T coff = T(1) / len;
        return Quaternion(w_ * coff, x_ * coff, y_ * coff, z_ * coff);
    }

    template <typename T>
    Rotator<T> Quaternion<T>::ToRotator() const
    {
        T sin_roll = T(2) * (w_ * z_ + x_ * y_);
        T cos_roll = T(1) - T(2) * (y_ * y_ + z_ * z_);
        T roll = RadianToDegree(std::atan2(sin_roll, cos_roll));

        T sin_pitch = T(2) * (w_ * x_ + y_ * z_);
        T cos_pitch = T(1) - T(2) * (x_ * x_ + y_ * y_);
        T pitch = RadianToDegree(std::atan2(sin_pitch, cos_pitch));

        T sin_yaw = T(2) * (w_ * y_ - z_ * x_);
        sin_yaw = std::clamp(sin_yaw, T(-1), T(1)); 
        T yaw = RadianToDegree(std::asin(sin_yaw));

        return Rotator<T>(pitch, yaw, roll);
    }

    template <typename T>
    Quaternion<T> Quaternion<T>::Conjugate() const
    {
        return {w_, -x_, -y_, -z_};
    }

    template <typename T>
    Vector3<T> Quaternion<T>::RotateVector(const Vector3<T> &v) const
    {
        Quaternion<T> v_quat{T(0), v.x_, v.y_, v.z_};
        Quaternion<T> res_quat = this->GetNormailizedQuat() * v_quat * this->Conjugate().GetNormailizedQuat();
        return Vector3<T>(res_quat.x_, res_quat.y_, res_quat.z_);
    }

}

#endif // KPENGINE_RUNTIME_MATH_QUATERNION_H