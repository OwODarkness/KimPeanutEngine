#ifndef KPENGINE_RUNTIME_MATH_ROTATOR_H
#define KPENGINE_RUNTIME_MATH_ROTATOR_H

#include <concepts>

namespace kpengine::math
{
    template<std::floating_point T>class Vector3;
    template<std::floating_point T>class Quaternion;

    template <std::floating_point T>
    class Rotator
    {
    public:
        Rotator();
        Rotator(T pitch, T yaw, T roll) ;
        Rotator(const Rotator &rhs);

        Rotator operator+(const Rotator &rhs) const
        {
            return Rotator(pitch_ + rhs.pitch_, yaw_ + rhs.yaw_, roll_ + rhs.roll_);
        }

        bool operator==(const Rotator& rhs) const
        {
            return pitch_ == rhs.pitch_ && yaw_ == rhs.yaw_ && roll_ == rhs.roll_;
        }
        bool operator!=(const Rotator& rhs) const
        {
            return !(*this == rhs);
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


}
#include "rotator.tpp"
#endif
