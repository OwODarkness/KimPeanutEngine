#include "vector4.h"
#include "vector3.h"

namespace kpengine::math
{
    template <typename T>
    Vector4<T>::Vector4() : x_{}, y_{}, z_{}, w_{} {}

    template <typename T>
    Vector4<T>::Vector4(T value) : x_(value), y_(value), z_(value), w_(value)
    {
    }

    template <typename T>
    Vector4<T>::Vector4(T x, T y, T z, T w) : x_(x), y_(y), z_(z), w_(w)
    {
    }

    template <typename T>
    Vector4<T>::Vector4(const Vector3<T> &v, T w) : x_(v.x_), y_(v.y_), z_(v.z_), w_(w)
    {
    }

    template <typename T>
    Vector4<T>::Vector4(const T arr[4]) : x_(arr[0]), y_(arr[1]), z_(arr[2]), w_(arr[3])
    {
    }

    template <typename T>
    T Vector4<T>::SquareLength() const
    {
        return x_ * x_ + y_ * y_ + z_ * z_ + w_ * w_;
    }

    template <typename T>
    T Vector4<T>::Norm() const
    {
        return std::sqrt(SquareLength());
    }

    template <typename T>
    T Vector4<T>::DotProduct(const Vector4 &v)
    {
        return x_ * v.x_ + y_ * v.y_ + z_ * v.z_ + w_ * v.w_;
    }

    template <typename T>
    void Vector4<T>::Normalize()
    {
        T length = Norm();
        if (length == 0.)
        {
            return;
        }
        T coeff = static_cast<T>(1.0 / length);
        x_ *= coeff;
        y_ *= coeff;
        z_ *= coeff;
        w_ *= coeff;
    }

    template <typename T>
    Vector3<T> Vector4<T>::MakeVector3() const
    {
        return Vector3<T>(x_, y_, z_);
    }

    template <typename T>
    Vector4<T>::operator Vector3<T>() const
    {
        return Vector3<T>(x_, y_, z_);
    }

}