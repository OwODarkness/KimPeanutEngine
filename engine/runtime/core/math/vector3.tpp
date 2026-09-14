#include "vector3.h"
#include <cmath>
namespace kpengine::math
{
    template <std::floating_point T>
    Vector3<T>::Vector3() : x_{}, y_{}, z_{} {}
    template <std::floating_point T>
    Vector3<T>::Vector3(T value) : x_(value), y_(value), z_(value) {}
    template <std::floating_point T>
    Vector3<T>::Vector3(T x, T y, T z) : x_(x), y_(y), z_(z) {}
    template <std::floating_point T>
    Vector3<T>::Vector3(std::span<const T, 3> values)
        : x_(values[0]), y_(values[1]), z_(values[2]) {}

    template <std::floating_point T>
    T Vector3<T>::SquareLength() const { return x_ * x_ + y_ * y_ + z_ * z_; }
    template <std::floating_point T>
    T Vector3<T>::Norm() const { return std::sqrt(SquareLength()); }

    template <std::floating_point T>
    T Vector3<T>::DotProduct(const Vector3 &v) const
    {
        return x_ * v.x_ + y_ * v.y_ + z_ * v.z_;
    }

    template <std::floating_point T>
    Vector3<T> Vector3<T>::CrossProduct(const Vector3 &v) const
    {
        return Vector3(
            y_ * v.z_ - z_ * v.y_,
            z_ * v.x_ - x_ * v.z_,
            x_ * v.y_ - y_ * v.x_);
    }

    template <std::floating_point T>
    Vector3<T> Vector3<T>::Reflect(const Vector3 &Normal) const
    {
        return this->operator-(2 * this->DotProduct(Normal) * Normal);
    }

    template <std::floating_point T>
    void Vector3<T>::Normalize()
    {
        T length = Norm();
        if (length == 0.)
        {
            return;
        }
        T coff = T(1.0 / length);
        x_ *= coff;
        y_ *= coff;
        z_ *= coff;
    }

    template <std::floating_point T>
    void Vector3<T>::SafetyNormalize()
    {
        double length = Norm();
        if (length == 0.)
        {
            length += 1e-4;
            return;
        }
        T coff = T(1.0 / length);
        x_ *= coff;
        y_ *= coff;
        z_ *= coff;
    }

    template <std::floating_point T>
    Vector3<T> Vector3<T>::GetSafetyNormalize() const
    {
        double length = Norm();
        if (length == 0.)
        {
            length += 1e-4;
        }
        T coff = T(1.0 / length);
        return Vector3(coff * x_, coff * y_, coff * z_);
    }

}
