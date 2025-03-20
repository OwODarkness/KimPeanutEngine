#ifndef KPENGINE_RUNTIME_MATH_MATRIX4_H
#define KPENGINE_RUNTIME_MATH_MATRIX4_H

#include <array>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include "runtime/core/math/math.h"
#include "runtime/core/math/vector4.h"
#include "runtime/core/math/matrix3.h"
namespace kpengine
{
    namespace math
    {

        template <typename T>
        class Matrix4
        {
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
            explicit Matrix4(const Matrix3<T>& m)
            {
                for(size_t i = 0 ; i<3;i++)
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
                assert(row_index < 4);
                return data_[row_index].data();
            }

            const T *operator[](size_t row_index) const
            {
                assert(row_index < 4);
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

            template<typename U, typename V>
            friend Matrix4<V> operator+(U scalar, const Matrix4<V>& mat);
    
            template<typename U, typename V>
            friend Matrix4<V> operator-(U scalar, const Matrix4<V>& mat);
    
            template<typename U, typename V>
            friend Matrix4<V> operator*(U scalar, const Matrix4<V>& mat);
    
            Matrix4 Transpose() const;
            T GetMinor(size_t r0, size_t r1, size_t r2, size_t c0, size_t c1, size_t c2) const;
            Matrix3<T> GetSubMatrix(size_t row_index, size_t col_index) const;
            Matrix3<T> GetMatrix3() const;
            T Determinant() const;
            Matrix4 Adjoint() const;
            Matrix4 Inverse() const;
            static Matrix4 Identity();
            static Matrix4 Zero();

        private:
            std::array<std::array<T, 4>, 4> data_;
        };
        template<typename T>
        Matrix4<T>  Matrix4<T>::Identity()
        {
            return Matrix4{
                T(1), 0, 0, 0,
                0, T(1), 0, 0,
                0, 0, T(1), 0,
                0, 0, 0, T(1)};
        }
        template<typename T>
         Matrix4<T> Matrix4<T>::Zero()
        {
            return Matrix4{
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0};
        }

        template<typename T, typename U>
        Matrix4<U> operator+(T scalar, const Matrix4<U>& mat)
        {
            Matrix4<U> res;
            U scalar_u = static_cast<U>(scalar);
            for(size_t i = 0;i<4;i++)
            {
                for(size_t j = 0;j<4;j++)
                {
                    res[i][j] = scalar_u + mat[i][j];
                }
            }
            return res;
        }
    
        template<typename T, typename U>
        Matrix4<U> operator-(T scalar, const Matrix4<U>& mat)
        {
            Matrix4<U> res;
            U scalar_u = static_cast<U>(scalar);
            for(size_t i = 0;i<4;i++)
            {
                for(size_t j = 0;j<4;j++)
                {
                    res[i][j] = scalar_u - mat[i][j];
                }
            }
            return res;
        }
    
        template<typename T, typename U>
        Matrix4<U> operator*(T scalar, const Matrix4<U>& mat)
        {
            Matrix4<U> res;
            U scalar_u = static_cast<U>(scalar);
            for(size_t i = 0;i<4;i++)
            {
                for(size_t j = 0;j<4;j++)
                {
                    res[i][j] = scalar_u * mat[i][j];
                }
            }
            return res;
        }

        template<typename T>
        Matrix4<T> Matrix4<T>::Transpose() const
        {
            Matrix4 res;
            for(size_t i = 0;i<4;i++)
            {
                for(size_t j = 0;j<4;j++)
                {
                    res[i][j] = data_[j][i];
                }
            }
            return res;
        }
        template<typename T>
        T Matrix4<T>::GetMinor(size_t r0, size_t r1, size_t r2, size_t c0, size_t c1, size_t c2) const
        {
            return 
                data_[r0][c0] * (data_[r1][c1] * data_[r2][c2] - data_[r1][c2] * data_[r2][c1]) + 
                data_[r0][c1] * (data_[r1][c2] * data_[r2][c0] - data_[r1][c0] * data_[r2][c2]) +
                data_[r0][c2] * (data_[r1][c0] * data_[r2][c1] - data_[r1][c1] * data_[r2][c0]);
        }
        template<typename T>
        Matrix3<T> Matrix4<T>::GetSubMatrix(size_t row_index, size_t col_index) const
        {
            Matrix3<T> res;
            size_t count = 0;
            for(size_t i = 0;i<4;i++)
            {
                for(size_t j = 0;j<4;j++)
                {
                    if(i == row_index || j == col_index)
                    {
                        continue;
                    }
                    else
                    {
                        res[count/3][count%3] = data_[i][j];
                        count++;
                        if(count == 10)
                        {
                            return res;
                        }
                    }
                }
            }
            return res;
        }

        template<typename T>
        Matrix3<T>  Matrix4<T>::GetMatrix3() const
        {
            Matrix3<T> res;
            for(size_t i = 0;i<3;i++)
            {
                for(size_t j = 0;j<3;j++)
                {
                    res[i][j] = data_[i][j];
                }
            }
            return res;
        }

        template<typename T>
        T Matrix4<T>::Determinant() const
        {
            return
                data_[0][0] * GetMinor(1, 2, 3, 1, 2, 3) -
                data_[0][1] * GetMinor(0, 2, 3, 1, 2, 3) + 
                data_[0][2] * GetMinor(0, 1, 3, 1, 2, 3) -
                data_[0][3] * GetMinor(0, 1, 2, 1, 2, 3);
        }

        template<typename T>
        Matrix4<T> Matrix4<T>::Adjoint() const
        {
            return Matrix4({
                GetMinor(1, 2, 3, 1, 2, 3),
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
                GetMinor(0, 1, 2, 0, 1, 2)
            });
        }

        template<typename T>
        Matrix4<T> Matrix4<T>::Inverse() const
        {
            T det = Determinant();
            if(det == 0)
            {
                throw std::runtime_error("determinant is zero");
            }
            T coff = 1.f / det;
            return coff * Adjoint();
        }
    }
} // namespace kpengine::math

#endif
