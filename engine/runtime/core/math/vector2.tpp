#include "vector2.h"
#include <cmath>
namespace kpengine::math
{
    template <typename T>
    Vector2<T>::Vector2() : x_{}, y_{} {}
    template <typename T>
    Vector2<T>::Vector2(T value) : x_(value), y_(value) {}
    template <typename T>
    Vector2<T>::Vector2(T x, T y) : x_(x), y_(y) {}
    template <typename T>
    Vector2<T>::Vector2(const T arr[2]) : x_(arr[0]), y_(arr[1]) {}

    template <typename T>
    T Vector2<T>::SquareLength() const
    {
        return x_ * x_ + y_ * y_;
    }

    template <typename T>
    T Vector2<T>::Norm() const
    {
        return std::sqrt(SquareLength());
    }

    template <typename T>
    T Vector2<T>::DotProduct(const Vector2 &v) const
    {
        return x_ * v.x_ + y_ * v.y_;
    }

    template <typename T>
    void Vector2<T>::Normalize()
    {
        T length = Norm();
        if (length == 0.)
        {
            return;
        }

        T coeff = static_cast<T>(1. / length);
        x_ *= coeff;
        y_ *= coeff;
    }

}
