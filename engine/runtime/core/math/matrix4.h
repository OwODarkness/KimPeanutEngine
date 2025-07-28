#ifndef KPENGINE_RUNTIME_MATH_MATRIX4_H
#define KPENGINE_RUNTIME_MATH_MATRIX4_H

#include <array>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include "runtime/core/math/math.h"
#include "runtime/core/math/vector4.h"
#include "runtime/core/math/matrix3.h"
#include "runtime/core/math/rotator.h"
#include "runtime/core/math/quaternion.h"
#include "runtime/core/math/transform.h"

namespace kpengine::math
{
    template <typename T>
    class Matrix4
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Matrix4()
        {
            const T k_zero = T(0);
            for (size_t i = 0; i < 4; ++i)
            {
                data_[i].fill(k_zero);
            }
        }

        Matrix4(const Matrix4 &m) : data_(m.data_) {}

        explicit Matrix4(const T (&arr)[4][4])
        {
            for (size_t i = 0; i < 4; ++i)
            {
                std::copy(arr[i], arr[i] + 4, data_[i].begin());
            }
        }
        explicit Matrix4(const Matrix3<T> &m)
        {
            for (size_t i = 0; i < 3; i++)
            {
                std::copy(m[i], m[i] + 3, data_[i].begin());
                data_[i][3] = 0;
            }
            data_[3].fill(0);
            data_[3][3] = T(1);
        }

        Matrix4(std::initializer_list<T> list)
        {
            auto it = list.begin();
            for (size_t i = 0; i < 4; ++i)
            {
                std::copy(it, it + 4, data_[i].begin());
                it += 4;
            }
        }

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

        bool operator==(const Matrix4 &mat) const
        {
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    if (!kpengine::math::IsNearlyEqual(data_[i][j], mat[i][j]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool operator!=(const Matrix4 &mat) const
        {
            return !(*this == mat);
        }

        Matrix4 operator+(T scalar) const
        {
            Matrix4 res;
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    res[i][j] = data_[i][j] + scalar;
                }
            }
            return res;
        }

        Matrix4 operator-(T scalar) const
        {
            return *this + (-scalar);
        }

        Matrix4 operator*(T scalar) const
        {
            Matrix4 res;
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    res[i][j] = data_[i][j] * scalar;
                }
            }
            return res;
        }

        Matrix4 operator/(T scalar) const
        {
            return *this * (T(1) / scalar);
        }

        Matrix4 operator+(const Matrix4 &mat) const
        {
            Matrix4 res;
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    res[i][j] = data_[i][j] + mat[i][j];
                }
            }
            return res;
        }

        Matrix4 operator-(const Matrix4 &mat) const
        {
            Matrix4 res;
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    res[i][j] = data_[i][j] - mat[i][j];
                }
            }
            return res;
        }

        Matrix4 operator-() const
        {
            Matrix4 res;
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    res[i][j] = -1 * data_[i][j];
                }
            }
            return res;
        }

        Matrix4 operator*(const Matrix4 &mat) const
        {
            Matrix4 res;
            for (size_t i = 0; i < 4; i++)
            {
                for (size_t j = 0; j < 4; j++)
                {
                    res[i][j] = 0;
                    for (size_t k = 0; k < 4; k++)
                    {
                        res[i][j] += data_[i][k] * mat[k][j];
                    }
                }
            }
            return res;
        }

        Vector4<T> operator*(const Vector4<T> &v) const
        {
            Vector4<T> res;
            for (size_t i = 0; i < 4; i++)
            {
                res[i] = data_[i][0] * v[0] + data_[i][1] * v[1] + data_[i][2] * v[2] + data_[i][3] * v[3];
            }
            return res;
        }

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

    template <typename T, typename U>
    Matrix4<U> operator+(T scalar, const Matrix4<U> &mat)
    {
        Matrix4<U> res;
        U scalar_u = static_cast<U>(scalar);
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                res[i][j] = scalar_u + mat[i][j];
            }
        }
        return res;
    }

    template <typename T, typename U>
    Matrix4<U> operator-(T scalar, const Matrix4<U> &mat)
    {
        Matrix4<U> res;
        U scalar_u = static_cast<U>(scalar);
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                res[i][j] = scalar_u - mat[i][j];
            }
        }
        return res;
    }

    template <typename T, typename U>
    Matrix4<U> operator*(T scalar, const Matrix4<U> &mat)
    {
        Matrix4<U> res;
        U scalar_u = static_cast<U>(scalar);
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                res[i][j] = scalar_u * mat[i][j];
            }
        }
        return res;
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::Transpose() const
    {
        Matrix4 res;
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                res[i][j] = data_[j][i];
            }
        }
        return res;
    }
    template <typename T>
    T Matrix4<T>::GetMinor(size_t r0, size_t r1, size_t r2, size_t c0, size_t c1, size_t c2) const
    {
        return data_[r0][c0] * (data_[r1][c1] * data_[r2][c2] - data_[r1][c2] * data_[r2][c1]) +
               data_[r0][c1] * (data_[r1][c2] * data_[r2][c0] - data_[r1][c0] * data_[r2][c2]) +
               data_[r0][c2] * (data_[r1][c0] * data_[r2][c1] - data_[r1][c1] * data_[r2][c0]);
    }
    template <typename T>
    Matrix3<T> Matrix4<T>::GetSubMatrix(size_t row_index, size_t col_index) const
    {
        Matrix3<T> res;
        size_t count = 0;
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                if (i == row_index || j == col_index)
                {
                    continue;
                }
                else
                {
                    res[count / 3][count % 3] = data_[i][j];
                    count++;
                    if (count == 10)
                    {
                        return res;
                    }
                }
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix4<T>::GetMatrix3() const
    {
        Matrix3<T> res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j];
            }
        }
        return res;
    }

    template <typename T>
    T Matrix4<T>::Determinant() const
    {
        return data_[0][0] * GetMinor(1, 2, 3, 1, 2, 3) -
               data_[0][1] * GetMinor(0, 2, 3, 1, 2, 3) +
               data_[0][2] * GetMinor(0, 1, 3, 1, 2, 3) -
               data_[0][3] * GetMinor(0, 1, 2, 1, 2, 3);
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::Adjoint() const
    {
        return Matrix4({GetMinor(1, 2, 3, 1, 2, 3),
                        -GetMinor(0, 2, 3, 1, 2, 3),
                        GetMinor(0, 1, 3, 1, 2, 3),
                        -GetMinor(0, 1, 2, 1, 2, 3),

                        -GetMinor(1, 2, 3, 0, 2, 3),
                        GetMinor(0, 2, 3, 0, 2, 3),
                        -GetMinor(0, 1, 3, 0, 2, 3),
                        GetMinor(0, 1, 2, 0, 2, 3),

                        GetMinor(1, 2, 3, 0, 1, 3),
                        -GetMinor(0, 2, 3, 0, 1, 3),
                        GetMinor(0, 1, 3, 0, 1, 3),
                        -GetMinor(0, 1, 2, 0, 1, 3),

                        -GetMinor(1, 2, 3, 0, 1, 2),
                        GetMinor(0, 2, 3, 0, 1, 2),
                        -GetMinor(0, 1, 3, 0, 1, 2),
                        GetMinor(0, 1, 2, 0, 1, 2)});
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::Inverse() const
    {
        T det = Determinant();
        if (det == 0)
        {
            throw std::runtime_error("determinant is zero");
        }
        T coff = 1.f / det;
        return coff * Adjoint();
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::Identity()
    {
        return Matrix4{
            T(1), 0, 0, 0,
            0, T(1), 0, 0,
            0, 0, T(1), 0,
            0, 0, 0, T(1)};
    }
    template <typename T>
    Matrix4<T> Matrix4<T>::Zero()
    {
        return Matrix4{
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0};
    }
    template <typename T>
    Matrix4<T> Matrix4<T>::MakeRotationMatrixOld(const Rotator<T> &rotator)
    {
        Matrix4 mat_roll = Matrix4::Identity();
        T roll_radian = DegreeToRadian(rotator.roll_);
        T cos_roll = std::cos(roll_radian);
        T sin_roll = std::sin(roll_radian);
        mat_roll[0][0] = cos_roll;
        mat_roll[0][1] = -sin_roll;
        mat_roll[1][0] = sin_roll;
        mat_roll[1][1] = cos_roll;

        // TODO pitch、yaw
        Matrix4 mat_pitch = Matrix4::Identity();
        T pitch_radian = DegreeToRadian(rotator.pitch_);
        T cos_pitch = std::cos(pitch_radian);
        T sin_pitch = std::sin(pitch_radian);
        mat_pitch[1][1] = cos_pitch;
        mat_pitch[1][2] = -sin_pitch;
        mat_pitch[2][1] = sin_pitch;
        mat_pitch[2][2] = cos_pitch;

        Matrix4 mat_yaw = Matrix4::Identity();
        T yaw_radian = DegreeToRadian(rotator.yaw_);
        T cos_yaw = std::cos(yaw_radian);
        T sin_yaw = std::sin(yaw_radian);
        mat_yaw[0][0] = cos_yaw;
        mat_yaw[0][2] = sin_yaw;
        mat_yaw[2][0] = -sin_yaw;
        mat_yaw[2][2] = cos_yaw;
        return mat_roll * mat_yaw* mat_pitch ;
    }

    // TODO: Gimbal Lock
template <typename T>
Matrix4<T> Matrix4<T>::MakeRotationMatrix(const Rotator<T>& rotator)
{
    Quaternion<T> quat = rotator.ToQuat();
    quat.Normalize();

    const T x = quat.x_;
    const T y = quat.y_;
    const T z = quat.z_;
    const T w = quat.w_;

    // Compute squared components more precisely
    const T xx = x * x;
    const T yy = y * y;
    const T zz = z * z;
    const T xy = x * y;
    const T xz = x * z;
    const T yz = y * z;
    const T wx = w * x;
    const T wy = w * y;
    const T wz = w * z;

    Matrix4<T> mat = Matrix4::Identity();

    // First row (X basis vector)
    mat[0][0] = T(1) - T(2) * (yy + zz);
    mat[0][1] = T(2) * (xy - wz);
    mat[0][2] = T(2) * (xz + wy);
    mat[0][3] = T(0);

    // Second row (Y basis vector)
    mat[1][0] = T(2) * (xy + wz);
    mat[1][1] = T(1) - T(2) * (xx + zz);
    mat[1][2] = T(2) * (yz - wx);
    mat[1][3] = T(0);

    // Third row (Z basis vector)
    mat[2][0] = T(2) * (xz - wy);
    mat[2][1] = T(2) * (yz + wx);
    mat[2][2] = T(1) - T(2) * (xx + yy);
    mat[2][3] = T(0);

    // Fourth row (translation)
    mat[3][0] = T(0);
    mat[3][1] = T(0);
    mat[3][2] = T(0);
    mat[3][3] = T(1);

    // Clean up near-zero values
    const T threshold = static_cast<T>(1e-6);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::abs(mat[i][j]) < threshold) {
                mat[i][j] = T(0);
            }
        }
    }

    return mat;
}
    template <typename T>
    Matrix4<T> Matrix4<T>::MakeScaleMatrix(const Vector3<T> &scale)
    {
        Matrix4 res = Matrix4::Identity();
        res[0][0] = scale[0];
        res[1][1] = scale[1];
        res[2][2] = scale[2];
        return res;
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::MakeTranslationMatrix(const Vector3<T> &translate)
    {
        Matrix4 res = Matrix4::Identity();
        res[0][3] = translate[0];
        res[1][3] = translate[1];
        res[2][3] = translate[2];
        return res;
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::MakeTransformMatrix(const Transform<T> &transfrom)
    {
        return MakeTranslationMatrix(transfrom.position_) *
               MakeRotationMatrix(transfrom.rotator_) *
               MakeScaleMatrix(transfrom.scale_);
    }
    template <typename T>
    Matrix4<T> Matrix4<T>::MakeCameraMatrix(const Vector3<T> &eye_pos, const Vector3<T> &gaze_dir, const Vector3<T> &up)
    {
        Vector3<T> w = -(gaze_dir);
        Vector3<T> u = up.CrossProduct(w);
        Vector3<T> v = w.CrossProduct(u);

        Matrix4 res = Matrix4::Identity();
        res[0][0] = u[0];
        res[0][1] = u[1];
        res[0][2] = u[2];
        res[0][3] = -u.DotProduct(eye_pos);

        res[1][0] = v[0];
        res[1][1] = v[1];
        res[1][2] = v[2];
        res[1][3] = -v.DotProduct(eye_pos);

        res[2][0] = w[0];
        res[2][1] = w[1];
        res[2][2] = w[2];
        res[2][3] = -w.DotProduct(eye_pos);

        return res;
    }

    template <typename T>
    Matrix4<T> Matrix4<T>::MakeOrthProjMatrix(T left, T right, T bottom, T top, T near, T far)
    {
        Matrix4 res = Matrix4::Identity();
        res[0][0] = T(2) / (right - left);
        res[0][3] = (left + right) / (left - right);
        res[1][1] = T(2) / (top - bottom);
        res[1][3] = (bottom + top) / (bottom - top);
        res[2][2] = T(2) / (near - far);
        res[2][3] = (far + near) / (far - near);
        return res;
    }
    template <typename T>
    Matrix4<T> Matrix4<T>::MakePerProjMatrix(T fov, T aspect, T near, T far)
    {
        Matrix4 res = Matrix4::Zero();
        T half_radian = T(kpengine::math::DegreeToRadian(0.5 * fov));
        T top = std::tan(half_radian) * near;
        T bottom = -top;
        T right = aspect * top;
        T left = -right;
        res[0][0] = 2 * near / (right - left);
        res[0][2] = (left + right) / (left - right);
        res[1][1] = 2 * near / (top - bottom);
        res[1][2] = (bottom + top) / (bottom - top);
        res[2][2] = (far + near) / (near - far);
        res[2][3] = 2 * far * near / (near - far);
        res[3][2] = -1;
        return res;
    }
} // namespace kpengine::math

#endif
