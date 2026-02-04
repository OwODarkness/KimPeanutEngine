#ifndef KPENGINE_RUNTIME_MATH_VECTOR2_H
#define KPENGINE_RUNTIME_MATH_VECTOR2_H

#include <cstddef>
#include <cassert>

namespace kpengine::math
{

    template <typename T>
    class Vector2
    {

        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Vector2();
        explicit Vector2(T value);
        Vector2(T x, T y);
        explicit Vector2(const T arr[2]);
        T operator[](size_t index) const
        {
            assert(index < 2);
            return *(&this->x_ + index);
        }

        T &operator[](size_t index)
        {
            assert(index < 2);
            return *(&this->x_ + index);
        }

        T *Data() { return &x_; }
        const T *Data() const { return &x_; }

        T SquareLength() const;
        T Norm() const;
        T DotProduct(const Vector2 &v) const;
        void Normalize();

        bool operator==(const Vector2 &v) const { return x_ == v.x_ && y_ == v.y_; }
        bool operator!=(const Vector2 &v) const { return x_ != v.x_ || y_ != v.y_; }

        Vector2 operator+(const Vector2 &v) const { return Vector2(x_ + v.x_, y_ + v.y_); }
        Vector2 operator-(const Vector2 &v) const { return Vector2(x_ - v.x_, y_ - v.y_); }
        Vector2 operator*(const Vector2 &v) const { return Vector2(x_ * v.x_, y_ * v.y_); }
        Vector2 operator+(T scalar) const { return Vector2(x_ + scalar, y_ + scalar); }
        Vector2 operator-(T scalar) const { return Vector2(x_ - scalar, y_ - scalar); }
        Vector2 operator*(T scalar) const { return Vector2(x_ * scalar, y_ * scalar); }
        Vector2 operator/(T scalar) const { return Vector2(x_ / scalar, y_ / scalar); }
        Vector2 operator-() const { return Vector2(-x_, -y_); }

        Vector2<T> &operator+=(T scalar)
        {
            x_ += scalar;
            y_ += scalar;
            return *this;
        }

        Vector2<T> &operator-=(T scalar)
        {
            x_ -= scalar;
            y_ -= scalar;
            return *this;
        }

        Vector2<T> &operator*=(T scalar)
        {
            x_ *= scalar;
            y_ *= scalar;
            return *this;
        }

        Vector2<T> &operator/=(T scalar)
        {
            assert(scalar != T(0));
            x_ /= scalar;
            y_ /= scalar;
            return *this;
        }

        Vector2<T> &operator+=(const Vector2 &v)
        {
            x_ += v.x_;
            y_ += v.y_;
            return *this;
        }

        Vector2<T> &operator-=(const Vector2 &v)
        {
            x_ -= v.x_;
            y_ -= v.y_;
            return *this;
        }

        Vector2<T> &operator*=(const Vector2 &v)
        {
            x_ *= v.x_;
            y_ *= v.y_;
            return *this;
        }

        Vector2<T> &operator/=(const Vector2 &v)
        {
            assert(v.x_ != T(0) && v.y_ != T(0));
            x_ /= v.x_;
            y_ /= v.y_;
            return *this;
        }

        template <typename U>
        friend Vector2<U> operator+(U scalar, const Vector2<U> &v);

        template <typename U, typename V>
        friend Vector2<U> operator+(U scalar, const Vector2<U> &v);

        template <typename U>
        friend Vector2<U> operator-(U scalar, const Vector2<U> &v);

        template <typename U, typename V>
        friend Vector2<U> operator-(U scalar, const Vector2<U> &v);

        template <typename U>
        friend Vector2<U> operator*(U scalar, const Vector2<U> &v);

        template <typename U, typename V>
        friend Vector2<U> operator*(U scalar, const Vector2<U> &v);

    public:
        T x_, y_;
    };

    template <typename T>
    Vector2<T> operator+(T scalar, const Vector2<T> &v)
    {
        return Vector2<T>(scalar + v.x_, scalar + v.y_);
    }

    template <typename T, typename U>
    Vector2<U> operator+(T scalar, const Vector2<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector2<U>(
            scalar_u + v.x_,
            scalar_u + v.y_);
    }

    template <typename T>
    Vector2<T> operator-(T scalar, const Vector2<T> &v)
    {
        return Vector2<T>(scalar - v.x_, scalar - v.y_);
    }

    template <typename T, typename U>
    Vector2<U> operator-(T scalar, const Vector2<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector2<U>(
            scalar_u - v.x_,
            scalar_u - v.y_);
    }

    template <typename T>
    Vector2<T> operator*(T scalar, const Vector2<T> &v)
    {
        return Vector2<T>(scalar * v.x_, scalar * v.y_);
    }

    template <typename T, typename U>
    Vector2<U> operator*(T scalar, const Vector2<U> &v)
    {
        U scalar_u = static_cast<U>(scalar);
        return Vector2<U>(
            scalar_u * v.x_,
            scalar_u * v.y_);
    }

}

#endif
