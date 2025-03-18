#ifndef KPENGINE_RUNTIME_MATH_VECTOR4_H
#define KPENGINE_RUNTIME_MATH_VECTOR4_H

#include <cstddef>
#include <cassert>
#include <cmath>

#include "vector3.h"

namespace kpengine {
    namespace math {
        template<typename T>
        class Vector4 {
        public:
            Vector4() : x_{}, y_{}, z_{}, w_{} {}
            Vector4(T value) : x_(value), y_(value), z_(value), w_(value) {}
            Vector4(T x, T y, T z, T w) : x_(x), y_(y), z_(z), w_(w) {}
            Vector4(const Vector3<T>&v, T w):x_(v.x_), y_(v.y_), z_(v.z_), w_(w){} 
            explicit Vector4(const T arr[4]) : x_(arr[0]), y_(arr[1]), z_(arr[2]), w_(arr[3]) {}

            T operator[](size_t index) const {
                assert(index < 4);
                return *(&x_ + index);
            }

            T& operator[](size_t index) {
                assert(index < 4);
                return *(&x_ + index);
            }

            T* Data() { return &x_; }
            const T* Data() const { return &x_; }

            inline double SquareLength() const { return x_ * x_ + y_ * y_ + z_ * z_ + w_ * w_; }
            double Length() const { return std::sqrt(SquareLength()); }

            bool operator==(const Vector4& v) const { return x_ == v.x_ && y_ == v.y_ && z_ == v.z_ && w_ == v.w_; }
            bool operator!=(const Vector4& v) const { return !(*this == v); }

            Vector4 operator+(const Vector4& v) noexcept { return Vector4(x_ + v.x_, y_ + v.y_, z_ + v.z_, w_ + v.w_); }
            Vector4 operator-(const Vector4& v) noexcept { return Vector4(x_ - v.x_, y_ - v.y_, z_ - v.z_, w_ - v.w_); }
            Vector4 operator*(const Vector4& v) noexcept { return Vector4(x_ * v.x_, y_ * v.y_, z_ * v.z_, w_ * v.w_); }
            Vector4 operator*(T scalar) noexcept { return Vector4(x_ * scalar, y_ * scalar, z_ * scalar, w_ * scalar); }
            Vector4 operator/(T scalar) {
                assert(scalar != 0);
                return Vector4(x_ / scalar, y_ / scalar, z_ / scalar, w_ / scalar);
            }
            Vector4 operator-() { return Vector4(-x_, -y_, -z_, -w_); }

            Vector4& operator+=(const Vector4& v);
            Vector4& operator-=(const Vector4& v);
            Vector4& operator*=(const Vector4& v);
            Vector4& operator/=(const Vector4& v);
            Vector4& operator*=(T scalar);
            Vector4& operator/=(T scalar);

            T DotProduct(const Vector4& v);
            void Normalize();

        public:
            T x_, y_, z_, w_;
        };

        template<typename T>
        Vector4<T>& Vector4<T>::operator+=(const Vector4& v) {
            x_ += v.x_; y_ += v.y_; z_ += v.z_; w_ += v.w_;
            return *this;
        }

        template<typename T>
        Vector4<T>& Vector4<T>::operator-=(const Vector4& v) {
            x_ -= v.x_; y_ -= v.y_; z_ -= v.z_; w_ -= v.w_;
            return *this;
        }

        template<typename T>
        Vector4<T>& Vector4<T>::operator*=(const Vector4& v) {
            x_ *= v.x_; y_ *= v.y_; z_ *= v.z_; w_ *= v.w_;
            return *this;
        }

        template<typename T>
        Vector4<T>& Vector4<T>::operator/=(const Vector4& v) {
            assert(v.x_ != 0 && v.y_ != 0 && v.z_ != 0 && v.w_ != 0);
            x_ /= v.x_; y_ /= v.y_; z_ /= v.z_; w_ /= v.w_;
            return *this;
        }

        template<typename T>
        Vector4<T>& Vector4<T>::operator*=(T scalar) {
            x_ *= scalar; y_ *= scalar; z_ *= scalar; w_ *= scalar;
            return *this;
        }

        template<typename T>
        Vector4<T>& Vector4<T>::operator/=(T scalar) {
            assert(scalar != 0);
            x_ /= scalar; y_ /= scalar; z_ /= scalar; w_ /= scalar;
            return *this;
        }

        template<typename T>
        T Vector4<T>::DotProduct(const Vector4& v) {
            return x_ * v.x_ + y_ * v.y_ + z_ * v.z_ + w_ * v.w_;
        }

        template<typename T>
        void Vector4<T>::Normalize() {
            double length = Length();
            if (length == 0) return;
            T coeff = static_cast<T>(1.0 / length);
            x_ *= coeff; y_ *= coeff; z_ *= coeff; w_ *= coeff;
        }
    }
}

#endif
