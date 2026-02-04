#ifndef KPENGINE_RUNTIME_MATH_VECTOR4_H
#define KPENGINE_RUNTIME_MATH_VECTOR4_H

#include <cstddef>
#include <cassert>
#include <cmath>


namespace kpengine::math
{
    extern template class Vector3<float>;

    template <typename T>
    class Vector4
    {

        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Vector4();
        explicit Vector4(T value);
        Vector4(T x, T y, T z, T w);
        Vector4(const Vector3<T> &v, T w);
        explicit Vector4(const T arr[4]);

        T SquareLength() const;
        T Norm() const;
        T DotProduct(const Vector4 &v);
        void Normalize();

        Vector3<T> MakeVector3() const;

        operator Vector3<T>() const;

        T operator[](size_t index) const
        {
            assert(index >= 0 && index < 4);
            return *(&x_ + index);
        }

        T &operator[](size_t index)
        {
            assert(index >= 0 && index < 4);
            return *(&x_ + index);
        }

        T *Data() { return &x_; }
        const T *Data() const { return &x_; }

        bool operator==(const Vector4 &v) const { return x_ == v.x_ && y_ == v.y_ && z_ == v.z_ && w_ == v.w_; }
        bool operator!=(const Vector4 &v) const { return !(*this == v); }

        Vector4 operator+(const Vector4 &v) noexcept { return Vector4(x_ + v.x_, y_ + v.y_, z_ + v.z_, w_ + v.w_); }
        Vector4 operator-(const Vector4 &v) noexcept { return Vector4(x_ - v.x_, y_ - v.y_, z_ - v.z_, w_ - v.w_); }
        Vector4 operator*(const Vector4 &v) noexcept { return Vector4(x_ * v.x_, y_ * v.y_, z_ * v.z_, w_ * v.w_); }

        Vector4 operator+(T scalar) noexcept { return Vector4(x_ + scalar, y_ + scalar, z_ + scalar, w_ + scalar); }
        Vector4 operator-(T scalar) noexcept { return Vector4(x_ - scalar, y_ - scalar, z_ - scalar, w_ - scalar); }
        Vector4 operator*(T scalar) noexcept { return Vector4(x_ * scalar, y_ * scalar, z_ * scalar, w_ * scalar); }
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

        template <typename U>
        friend Vector4<U> operator+(U scalar, const Vector4<U> &v);

        template <typename U, typename V>
        friend Vector4<V> operator+(U scalar, const Vector4<V> &v);

        template <typename U>
        friend Vector4<U> operator-(U scalar, const Vector4<U> &v);

        template <typename U, typename V>
        friend Vector4<V> operator-(U scalar, const Vector4<V> &v);

        template <typename U>
        friend Vector4<U> operator*(U scalar, const Vector4<U> &v);

        template <typename U, typename V>
        friend Vector4<V> operator*(U scalar, const Vector4<V> &v);

    public:
        T x_, y_, z_, w_;
    };

    template <typename T>
    Vector4<T> operator+(T scalar, const Vector4<T> &v)
    {
        return Vector4<T>(v.x_ + scalar, v.y_ + scalar, v.z_ + scalar, v.w_ + scalar);
    }

    template <typename T, typename U>
    Vector4<U> operator+(T scalar, const Vector4<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector4<U>(
            scalar_u + v.x_,
            scalar_u + v.y_,
            scalar_u + v.z_,
            scalar_u + v.w_);
    }

    template <typename T>
    Vector4<T> operator-(T scalar, const Vector4<T> &v)
    {
        return Vector4<T>(scalar - v.x_, scalar - v.y_, scalar - v.z_, scalar - v.w_);
    }

    template <typename T, typename U>
    Vector4<U> operator-(T scalar, const Vector4<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector4<U>(
            scalar_u - v.x_,
            scalar_u - v.y_,
            scalar_u - v.z_,
            scalar_u - v.w_);
    }

    template <typename T>
    Vector4<T> operator*(T scalar, const Vector4<T> &v)
    {
        return Vector4<T>(v.x_ * scalar, v.y_ * scalar, v.z_ * scalar, v.w_ * scalar);
    }

    template <typename T, typename U>
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

#endif
