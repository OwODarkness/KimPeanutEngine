#ifndef KPENGINE_RUNTIME_MATH_VECTOR2_H
#define KPENGINE_RUNTIME_MATH_VECTOR2_H

#include <cstddef>
#include <cassert>
#include "math.h"

namespace kpengine{
    namespace math{
        template<typename T>
        class Vector2{
        public:
            Vector2(): x_{}, y_{} {}
            Vector2(T value): x_(value), y_(value) {}
            Vector2(T x, T y): x_(x), y_(y) {}
            explicit Vector2(const T arr[2]): x_(arr[0]), y_(arr[1]) {}

            T operator[](size_t index) const
            {
                assert(index < 2);
                return *(&this->x_ + index);
            }

            T& operator[](size_t index)
            {
                assert(index < 2);
                return *(&this->x_ + index);
            }

            T* Data() { return &x_; }
            const T* Data() const { return &x_; }

            inline double SquareLength() const { return x_ * x_ + y_ * y_; }
            double Length() const { return std::sqrt(SquareLength()); }

            bool operator==(const Vector2& v) const { return x_ == v.x_ && y_ == v.y_; }
            bool operator!=(const Vector2& v) const { return x_ != v.x_ || y_ != v.y_; }
            
            Vector2 operator+(const Vector2& v) noexcept { return Vector2(x_ + v.x_, y_ + v.y_); }
            Vector2 operator-(const Vector2& v) noexcept { return Vector2(x_ - v.x_, y_ - v.y_); }
            Vector2 operator*(const Vector2& v) noexcept { return Vector2(x_ * v.x_, y_ * v.y_); }
            Vector2 operator*(T scalar) noexcept { return Vector2(x_ * scalar, y_ * scalar); }
            Vector2 operator/(T scalar) {
                return Vector2(x_ / scalar, y_ / scalar);
            }
            Vector2 operator-() { return Vector2(-x_, -y_); }

            Vector2& operator+=(T scalar);
            Vector2& operator-=(T scalar);
            Vector2& operator*=(T scalar);
            Vector2& operator/=(T scalar);

            Vector2& operator+=(const Vector2& v);
            Vector2& operator-=(const Vector2& v);
            Vector2& operator*=(const Vector2& v);
            Vector2& operator/=(const Vector2& v);

            T DotProduct(const Vector2& v);
            void Normalize();

            template<typename U>
            friend Vector2<U> operator+(U scalar, const Vector2<U>& v);

            template<typename U, typename V>
            friend Vector2<U> operator+(U scalar, const Vector2<U>& v);

            template<typename U>
            friend Vector2<U> operator-(U scalar, const Vector2<U>& v);

            template<typename U, typename V>
            friend Vector2<U> operator-(U scalar, const Vector2<U>& v);

            template<typename U>
            friend Vector2<U> operator*(U scalar, const Vector2<U>& v);

            template<typename U, typename V>
            friend Vector2<U> operator*(U scalar, const Vector2<U>& v);
        
        public:
            T x_, y_;
        };

        template<typename T>
        Vector2<T>& Vector2<T>::operator+=(T scalar)
        {
            x_ += scalar;
            y_ += scalar;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator-=(T scalar)
        {
            x_ -= scalar;
            y_ -= scalar;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator*=(T scalar)
        {
            x_ *= scalar;
            y_ *= scalar;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator/=(T scalar)
        {
            assert(!kpengine::IsNearlyZero(scalar));
            x_ /= scalar;
            y_ /= scalar;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator+=(const Vector2& v)
        {
            x_ += v.x_;
            y_ += v.y_;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator-=(const Vector2& v)
        {
            x_ -= v.x_;
            y_ -= v.y_;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator*=(const Vector2& v)
        {
            x_ *= v.x_;
            y_ *= v.y_;
            return *this;
        }

        template<typename T>
        Vector2<T>& Vector2<T>::operator/=(const Vector2& v)
        {
            assert(!kpengine::IsNearlyZero(v.x_) && !kpengine::IsNearlyZero(v.y_));
            x_ /= v.x_;
            y_ /= v.y_;
            return *this;
        }

        template<typename T>
        T Vector2<T>::DotProduct(const Vector2& v)
        {
            return x_ * v.x_ + y_ * v.y_;
        }

        template<typename T>
        void Vector2<T>::Normalize()
        {
            double length = Length();
            if (kpengine::IsNearlyZero(length))
            {
                return;
            }

            T coeff = static_cast<T>(1. / length);
            x_ *= coeff;
            y_ *= coeff;
        }

        template<typename T>
        Vector2<T> operator+(T scalar, const Vector2<T>& v)
        {
            return Vector2<T>(scalar + v.x_, scalar + v.y_);
        }

        template<typename T, typename U>
        Vector2<U> operator+(T scalar, const Vector2<U>& v)
        {
            U scalar_u = static_cast<U>(scalar);
            return Vector2<T>(
                scalar_u + v.x_,
                scalar_u + v.y_
            );
        }

        template<typename T>
        Vector2<T> operator-(T scalar, const Vector2<T>& v)
        {
            return Vector2<T>(scalar - v.x_, scalar - v.y_);
        }

        template<typename T, typename U>
        Vector2<U> operator-(T scalar, const Vector2<U>& v)
        {
            U scalar_u = static_cast<U>(scalar);
            return Vector2<T>(
                scalar_u - v.x_,
                scalar_u - v.y_
            );
        }

        template<typename T>
        Vector2<T> operator*(T scalar, const Vector2<T>& v)
        {
            return Vector2<T>(scalar * v.x_, scalar * v.y_);
        }

        template<typename T, typename U>
        Vector2<U> operator*(T scalar, const Vector2<U>& v)
        {
            U scalar_u = static_cast<U>(scalar);
            return Vector2<T>(
                scalar_u * v.x_,
                scalar_u * v.y_
            );
        }
    }   
}

#endif
