#include "vector2.h"
#include <cmath>
namespace kpengine::math
{
    template <std::floating_point T>
    Vector2<T>::Vector2() : x_{}, y_{} {}
    template <std::floating_point T>
    Vector2<T>::Vector2(T value) : x_(value), y_(value) {}
    template <std::floating_point T>
    Vector2<T>::Vector2(T x, T y) : x_(x), y_(y) {}
    template <std::floating_point T>
    Vector2<T>::Vector2(std::span<const T, 2> values) : x_(values[0]), y_(values[1]) {}

    template <std::floating_point T>
    T Vector2<T>::SquareLength() const
    {
        return x_ * x_ + y_ * y_;
    }

    template <std::floating_point T>
    T Vector2<T>::Norm() const
    {
        return std::sqrt(SquareLength());
    }

    template <std::floating_point T>
    T Vector2<T>::DotProduct(const Vector2 &v) const
    {
        return x_ * v.x_ + y_ * v.y_;
    }

    template <std::floating_point T>
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
