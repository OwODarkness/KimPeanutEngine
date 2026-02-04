#ifndef KPENGINE_RUNTIME_MATH_MATRIX4_H
#define KPENGINE_RUNTIME_MATH_MATRIX4_H

#include <array>
#include <cstddef>
#include <algorithm>
#include <cassert>


namespace kpengine::math
{
    template<typename T> class Matrix3;
    template<typename T> class Matrix4;
    template<typename T> class Rotator;
    template<typename T> class Transform;

    template <typename T>
    class Matrix4
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Matrix4();
        Matrix4(const Matrix4 &m);
        explicit Matrix4(const T (&arr)[4][4]);
        explicit Matrix4(const Matrix3<T> &m);
        Matrix4(std::initializer_list<T> list);

        T *operator[](size_t row_index)
        {
            assert(row_index >= 0 && row_index < 4);
            return data_[row_index].data();
        }

        const T *operator[](size_t row_index) const
        {
            assert(row_index >= 0 && row_index < 4);
            return data_[row_index].data();
        }

        bool operator==(const Matrix4 &mat) const;

        bool operator!=(const Matrix4 &mat) const;

        Matrix4 operator+(T scalar) const;
        Matrix4 operator-(T scalar) const;

        Matrix4 operator*(T scalar) const;

        Matrix4 operator/(T scalar) const;
        Matrix4 operator+(const Matrix4 &mat) const;
        Matrix4 operator-(const Matrix4 &mat) const;

        Matrix4 operator-() const;
        Matrix4 operator*(const Matrix4 &mat) const;

        Vector4<T> operator*(const Vector4<T> &v) const;
        template <typename U, typename V>
        friend Matrix4<V> operator+(U scalar, const Matrix4<V> &mat);

        template <typename U, typename V>
        friend Matrix4<V> operator-(U scalar, const Matrix4<V> &mat);

        template <typename U, typename V>
        friend Matrix4<V> operator*(U scalar, const Matrix4<V> &mat);

        Matrix4 Transpose() const;
        T GetMinor(size_t r0, size_t r1, size_t r2, size_t c0, size_t c1, size_t c2) const;
        Matrix3<T> GetSubMatrix(size_t row_index, size_t col_index) const;
        Matrix3<T> GetMatrix3() const;
        T Determinant() const;
        Matrix4 Adjoint() const;
        Matrix4 Inverse() const;
        static Matrix4 Identity();
        static Matrix4 Zero();
        static Matrix4 MakeRotationMatrixOld(const Rotator<T> &rotator);
        static Matrix4 MakeRotationMatrix(const Rotator<T> &rotator);
        static Matrix4 MakeScaleMatrix(const Vector3<T> &scale);
        static Matrix4 MakeTranslationMatrix(const Vector3<T> &translate);
        static Matrix4 MakeTransformMatrix(const Transform<T> &transform);
        static Matrix4 MakeCameraMatrix(const Vector3<T> &eye_pos, const Vector3<T> &gaze_dir, const Vector3<T> &up);
        // Orthographic Projection
        static Matrix4 MakeOrthProjMatrix(T left, T right, T bottom, T top, T near, T far);
        // Perspective Projection
        static Matrix4 MakePerProjMatrix(T fov, T aspect, T near, T far);

    private:
        std::array<std::array<T, 4>, 4> data_;
    };

    
} // namespace kpengine::math

#endif
