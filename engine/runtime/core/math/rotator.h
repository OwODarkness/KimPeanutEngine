#ifndef KPENGINE_RUNTIME_MATH_ROTATOR_H
#define KPENGINE_RUNTIME_MATH_ROTATOR_H

#include "math.h"
#include "vector3.h"
#include "quaternion.h"

namespace kpengine::math
{
    template <typename T>
    class Rotator
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Rotator() : pitch_(0), yaw_(0), roll_(0) {}
        Rotator(T pitch, T yaw, T roll) : pitch_(pitch), yaw_(yaw), roll_(roll) {}
        Rotator(const Rotator &rhs) : pitch_(rhs.pitch_), yaw_(rhs.yaw_), roll_(rhs.roll_) {}

        Rotator operator+(const Rotator &rhs) const
        {
            return Rotator(pitch_ + rhs.pitch_, yaw_ + rhs.yaw_, roll_ + rhs.roll_);
        }

        bool operator==(const Rotator& rhs) const
        {
            return pitch_ == rhs.pitch_ && yaw_ == rhs.yaw_ && roll_ == rhs.roll_;
        }

        Rotator &operator=(const Rotator &rhs)
        {
            pitch_ = rhs.pitch_;
            yaw_ = rhs.yaw_;
            roll_ = rhs.roll_;
            return *this;
        }

        Quaternion<T> ToQuat() const;
        Vector3<T> RotateVector(const Vector3<T> &v) const;

    public:
        T pitch_; // X-axis rotation (degrees)
        T yaw_;   // Y-axis rotation (degrees)
        T roll_;  // Z-axis rotation (degrees)
    };

    template <typename T>
    Quaternion<T> Rotator<T>::ToQuat() const
    {
        T half_pitch = DegreeToRadian(pitch_) * T(0.5);
        T half_yaw = DegreeToRadian(yaw_) * T(0.5);
        T half_roll = DegreeToRadian(roll_) * T(0.5);

        T cp = std::cos(half_pitch), sp = std::sin(half_pitch); 
        T cy = std::cos(half_yaw), sy = std::sin(half_yaw);     
        T cr = std::cos(half_roll), sr = std::sin(half_roll);   

        T w = cp * cy * cr + sp * sy * sr;
        T x = sp * cy * cr - cp * sy * sr;
        T y = cp * sy * cr + sp * cy * sr;
        T z = cp * cy * sr - sp * sy * cr;

        return Quaternion<T>(w, x, y, z);
    }

    template <typename T>
    Vector3<T> Rotator<T>::RotateVector(const Vector3<T> &v) const
    {
        Quaternion<T> quat = this->ToQuat();
        return quat.RotateVector(v);
    }
}

#endif