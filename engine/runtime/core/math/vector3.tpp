#include "vector3.h"
#include <cmath>
namespace kpengine::math
{
    template <typename T>
    Vector3<T>::Vector3() : x_{}, y_{}, z_{} {}
    template <typename T>
    Vector3<T>::Vector3(T value) : x_(value), y_(value), z_(value) {}
    template <typename T>
    Vector3<T>::Vector3(T x, T y, T z) : x_(x), y_(y), z_(z) {}
    template <typename T>
    Vector3<T>::Vector3(const T arr[3]) : x_(arr[0]), y_(arr[1]), z_(arr[2]) {}

    template <typename T>
    T Vector3<T>::SquareLength() const { return x_ * x_ + y_ * y_ + z_ * z_; }
    template <typename T>
    T Vector3<T>::Norm() const { return std::sqrt(SquareLength()); }

    template <typename T>
    T Vector3<T>::DotProduct(const Vector3 &v) const
    {
        return x_ * v.x_ + y_ * v.y_ + z_ * v.z_;
    }

    template <typename T>
    Vector3<T> Vector3<T>::CrossProduct(const Vector3 &v) const
    {
        return Vector3(
            y_ * v.z_ - z_ * v.y_,
            z_ * v.x_ - x_ * v.z_,
            x_ * v.y_ - y_ * v.x_);
    }

    template <typename T>
    Vector3<T> Vector3<T>::Reflect(const Vector3 &Normal) const
    {
        return this->operator-(2 * this->DotProduct(Normal) * Normal);
    }

    template <typename T>
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

    template <typename T>
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

    template <typename T>
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