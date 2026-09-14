#ifndef KPENGINE_RUNTIME_MATH_VECTOR4_H
#define KPENGINE_RUNTIME_MATH_VECTOR4_H

#include <cstddef>
#include <cassert>
#include <cmath>
#include <concepts>
#include <span>


namespace kpengine::math
{
    template <std::floating_point T>
    class Vector3;

    extern template class Vector3<float>;

    template <std::floating_point T>
    class Vector4
    {
    public:
        Vector4();
        explicit Vector4(T value);
        Vector4(T x, T y, T z, T w);
        Vector4(const Vector3<T> &v, T w);
        explicit Vector4(std::span<const T, 4> values);

        T SquareLength() const;
        T Norm() const;
        T DotProduct(const Vector4 &v) const;
        void Normalize();

        Vector3<T> MakeVector3() const;

        operator Vector3<T>() const;

        T operator[](size_t index) const
        {
            assert(index < 4);
            return index == 0 ? x_ : (index == 1 ? y_ : (index == 2 ? z_ : w_));
        }

        T &operator[](size_t index)
        {
            assert(index < 4);
            return index == 0 ? x_ : (index == 1 ? y_ : (index == 2 ? z_ : w_));
        }

        bool operator==(const Vector4 &v) const { return x_ == v.x_ && y_ == v.y_ && z_ == v.z_ && w_ == v.w_; }
        bool operator!=(const Vector4 &v) const { return !(*this == v); }

        Vector4 operator+(const Vector4 &v) const noexcept { return Vector4(x_ + v.x_, y_ + v.y_, z_ + v.z_, w_ + v.w_); }
        Vector4 operator-(const Vector4 &v) const noexcept { return Vector4(x_ - v.x_, y_ - v.y_, z_ - v.z_, w_ - v.w_); }
        Vector4 operator*(const Vector4 &v) const noexcept { return Vector4(x_ * v.x_, y_ * v.y_, z_ * v.z_, w_ * v.w_); }

        Vector4 operator+(T scalar) const noexcept { return Vector4(x_ + scalar, y_ + scalar, z_ + scalar, w_ + scalar); }
        Vector4 operator-(T scalar) const noexcept { return Vector4(x_ - scalar, y_ - scalar, z_ - scalar, w_ - scalar); }
        Vector4 operator*(T scalar) const noexcept { return Vector4(x_ * scalar, y_ * scalar, z_ * scalar, w_ * scalar); }
        Vector4 operator/(T scalar)
        {
            assert(scalar != T(0));
            return Vector4(x_ / scalar, y_ / scalar, z_ / scalar, w_ / scalar);
        }
        Vector4 operator-() const { return Vector4(-x_, -y_, -z_, -w_); }

        Vector4<T> &operator+=(const Vector4 &v)
        {
            x_ += v.x_;
            y_ += v.y_;
            z_ += v.z_;
            w_ += v.w_;
            return *this;
        }

        Vector4<T> &operator-=(const Vector4 &v)
        {
            x_ -= v.x_;
            y_ -= v.y_;
            z_ -= v.z_;
            w_ -= v.w_;
            return *this;
        }

        Vector4<T> &operator*=(const Vector4 &v)
        {
            x_ *= v.x_;
            y_ *= v.y_;
            z_ *= v.z_;
            w_ *= v.w_;
            return *this;
        }

        Vector4<T> &operator/=(const Vector4 &v)
        {
            assert(v.x_ != T(0) && v.y_ != T(0) && v.z_ != T(0) && v.w_ != T(0));
            x_ /= v.x_;
            y_ /= v.y_;
            z_ /= v.z_;
            w_ /= v.w_;
            return *this;
        }

        Vector4<T> &operator*=(T scalar)
        {
            x_ *= scalar;
            y_ *= scalar;
            z_ *= scalar;
            w_ *= scalar;
            return *this;
        }

        Vector4<T> &operator/=(T scalar)
        {
            assert(scalar != 0);
            x_ /= scalar;
            y_ /= scalar;
            z_ /= scalar;
            w_ /= scalar;
            return *this;
        }

        template <std::floating_point U>
        friend Vector4<U> operator+(U scalar, const Vector4<U> &v);

        template <typename U, std::floating_point V>
        friend Vector4<V> operator+(U scalar, const Vector4<V> &v);

        template <std::floating_point U>
        friend Vector4<U> operator-(U scalar, const Vector4<U> &v);

        template <typename U, std::floating_point V>
        friend Vector4<V> operator-(U scalar, const Vector4<V> &v);

        template <std::floating_point U>
        friend Vector4<U> operator*(U scalar, const Vector4<U> &v);

        template <typename U, std::floating_point V>
        friend Vector4<V> operator*(U scalar, const Vector4<V> &v);

    public:
        T x_, y_, z_, w_;
    };

    template <std::floating_point T>
    Vector4<T> operator+(T scalar, const Vector4<T> &v)
    {
        return Vector4<T>(v.x_ + scalar, v.y_ + scalar, v.z_ + scalar, v.w_ + scalar);
    }

    template <typename T, std::floating_point U>
    Vector4<U> operator+(T scalar, const Vector4<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector4<U>(
            scalar_u + v.x_,
            scalar_u + v.y_,
            scalar_u + v.z_,
            scalar_u + v.w_);
    }

    template <std::floating_point T>
    Vector4<T> operator-(T scalar, const Vector4<T> &v)
    {
        return Vector4<T>(scalar - v.x_, scalar - v.y_, scalar - v.z_, scalar - v.w_);
    }

    template <typename T, std::floating_point U>
    Vector4<U> operator-(T scalar, const Vector4<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector4<U>(
            scalar_u - v.x_,
            scalar_u - v.y_,
            scalar_u - v.z_,
            scalar_u - v.w_);
    }

    template <std::floating_point T>
    Vector4<T> operator*(T scalar, const Vector4<T> &v)
    {
        return Vector4<T>(v.x_ * scalar, v.y_ * scalar, v.z_ * scalar, v.w_ * scalar);
    }

    template <typename T, std::floating_point U>
    Vector3<U> operator*(T scalar, const Vector4<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector4<U>(
            scalar_u * v.x_,
            scalar_u * v.y_,
            scalar_u * v.z_,
            scalar_u * v.w_);
    }
}
#include "vector4.tpp"

#endif
