#ifndef KPENGINE_RUNTIME_MATH_MATRIX3_H
#define KPENGINE_RUNTIME_MATH_MATRIX3_H

#include <array>
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <type_traits>
namespace kpengine::math{

    template<typename T> class Vector3;
    extern template class Vector3<float>;
    extern template class Vector3<double>;


    template<typename T>
    class Matrix3{
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Matrix3();
        Matrix3(const Matrix3& m);
        explicit Matrix3(const T (&arr)[3][3]);
        Matrix3(const T (&arr)[9]);
        Matrix3(std::initializer_list<T> list);

        std::span<T, 3> operator[](size_t row_index) {
            assert(row_index < 3);
            return data_[row_index];
        }
        std::span<const T, 3> operator[](size_t row_index) const {
            assert(row_index < 3);
            return data_[row_index];
        }

        std::span<T, 3> Row(size_t row_index) noexcept
        {
            assert(row_index < 3);
            return data_[row_index];
        }

        std::span<const T, 3> Row(size_t row_index) const noexcept
        {
            assert(row_index < 3);
            return data_[row_index];
        }

        bool operator==(const Matrix3& mat) const;
        bool operator!=(const Matrix3& mat) const;

        Matrix3 operator+(T scalar) const;
        Matrix3 operator-(T scalar) const;
        Matrix3 operator*(T scalar) const;
        Matrix3 operator/(T scalar) const;
        Matrix3 operator+(const Matrix3& mat) const;
        Matrix3 operator-(const Matrix3& mat) const;
        Matrix3 operator-() const;
        Matrix3 operator*(const Matrix3& mat) const;
        Vector3<T> operator*(const Vector3<T>& v) const;
        
        Matrix3& operator+=(T scalar);
        Matrix3& operator-=(T scalar);
        Matrix3& operator*=(T scalar);
        Matrix3& operator/=(T scalar);

        Matrix3& operator+=(const Matrix3& mat);
        Matrix3& operator-=(const Matrix3& mat);

        

        template<typename U, typename V>
        friend Matrix3<V> operator+(U scalar, const Matrix3<V>& mat);

        template<typename U, typename V>
        friend Matrix3<V> operator-(U scalar, const Matrix3<V>& mat);

        template<typename U, typename V>
        friend Matrix3<V> operator*(U scalar, const Matrix3<V>& mat);

        Matrix3 Transpose() const;
        T Determinant() const;
        Matrix3 Adjoint() const;
        Matrix3 Inverse() const;        

        static Matrix3 Identity();
        static Matrix3 Zero();
    private:
        std::array<std::array<T, 3>, 3> data_;
    };

    
}
#include "matrix3.tpp" 
#endif
