#ifndef KPENGINE_RUNTIME_MATH_MATRIX3_H
#define KPENGINE_RUNTIME_MATH_MATRIX3_H

#include <array>
#include <cstddef>
#include <algorithm>
#include <stdexcept>

#include "runtime/core/math/math.h"
#include "runtime/core/math/vector3.h"
namespace kpengine{
    namespace math{
    template<typename T>
    class Matrix3{
    public:
        Matrix3(){
            const T k_zero = T(0);
            data_[0] = {k_zero, k_zero, k_zero};
            data_[1] = {k_zero, k_zero, k_zero};
            data_[2] = {k_zero, k_zero, k_zero};
        }
        Matrix3(const Matrix3& m):data_(m.data_){}
        explicit Matrix3(const T (&arr)[3][3])
        {
            std::copy(arr[0], arr[0] + 3, data_[0].begin());
            std::copy(arr[1], arr[1] + 3, data_[1].begin());
            std::copy(arr[2], arr[2] + 3, data_[2].begin());
        }
        Matrix3(T (&arr)[9])
        {
            std::copy(arr, arr + 3, data_[0].begin());
            std::copy(arr + 3, arr + 6, data_[1].begin());
            std::copy(arr + 6, arr + 9, data_[2].begin());
        }
        Matrix3(std::initializer_list<T> list){
            std::copy(list.begin(), list.begin() + 3, data_[0].begin());
            std::copy(list.begin() + 3, list.begin() + 6, data_[1].begin());
            std::copy(list.begin() + 6, list.end(), data_[2].begin());
        }

        T* operator[](size_t row_index) {
            assert(row_index >=0 && row_index <=2 );
            return data_[row_index].data();
        }
        const T* operator[](size_t row_index) const {
            assert(row_index >=0 && row_index <=2 );
            return data_[row_index].data();
        }

        bool operator==(const Matrix3& mat) const
        {
            for(size_t i = 0;i<3;i++)
            {
                for(size_t j = 0;j<3;j++)
                {
                    if(!kpengine::math::IsNearlyEqual(data_[i][j], mat[i][j]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool operator!=(const Matrix3& mat) const
        {
            return !this->operator==(mat);
        }

        Matrix3 operator+(T scalar) const
        {
            Matrix3 res;
            for(size_t i = 0;i<3;i++)
            {
                for(size_t j =0;j<3;j++)
                {
                    res[i][j] = data_[i][j] + scalar;
                }
            }
            return res;
        }

        Matrix3 operator-(T scalar) const
        {
            Matrix3 res;
            for(size_t i = 0;i<3;i++)
            {
                for(size_t j =0;j<3;j++)
                {
                    res[i][j] = data_[i][j] - scalar;
                }
            }
            return res;
        }

        Matrix3 operator*(T scalar) const
        {
            Matrix3 res;
            for(size_t i = 0;i<3;i++)
            {
                for(size_t j =0;j<3;j++)
                {
                    res[i][j] = data_[i][j] * scalar;
                }
            }
            return res;
        }

        Matrix3 operator/(T scalar) const
        {
            Matrix3 res;
            for(size_t i = 0;i<3;i++)
            {
                for(size_t j =0;j<3;j++)
                {
                    res[i][j] = data_[i][j] / scalar;
                }
            }
            return res;
        }


        Matrix3 operator+(const Matrix3& mat) const
        {
            Matrix3 res;
            for(size_t i = 0; i< 3;i++)
            {
                for(size_t j = 0;j< 3;j++)
                {
                    res[i][j] = data_[i][j] + mat[i][j];
                }
            }
            return res;
        }



        Matrix3 operator-(const Matrix3& mat) const
        {
            Matrix3 res;
            for(size_t i = 0; i< 3;i++)
            {
                for(size_t j = 0;j< 3;j++)
                {
                    res[i][j] = data_[i][j] - mat[i][j];
                }
            }
            return res;
        }

        Matrix3 operator-() const
        {
            Matrix3 res;
            for(size_t i = 0; i< 3;i++)
            {
                for(size_t j = 0;j< 3;j++)
                {
                    res[i][j] = -1 * data_[i][j];
                }
            }
            return res;
        }

        Matrix3 operator*(const Matrix3& mat) const
        {
            Matrix3 res;
            for(size_t i = 0;i<3;i++)
            {
                for(size_t k = 0;k<3;k++)
                {
                    T sum{};
                    for(size_t j = 0;j<3;j++)
                    {
                        sum += data_[i][j] * mat[j][k];
                    }
                    res[i][k] = sum;
                }
            }
            return res;
        }

        Vector3<T> operator*(const Vector3<T>& v)
        {
            Vector3<T> res;
            for(size_t i = 0;i<3;i++)
            {
                res[i] = data_[i][0] * v[0] + data_[i][1] * v[1] + data_[i][2] * v[2];
            }
            return res;
        }
        

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

    template<typename T>
    Matrix3<T> Matrix3<T>::Identity()
    {
        return Matrix3{
            T(1), 0, 0, 
            0, T(1), 0, 
            0, 0, T(1)};
    }
    
    template<typename T>
    Matrix3<T> Matrix3<T>::Zero()
    {
        return Matrix3{
            0, 0, 0, 
            0, 0, 0, 
            0, 0, 0};
    }

    template<typename T, typename U>
    Matrix3<U> operator+(T scalar, const Matrix3<U>& mat)
    {
        Matrix3<U> res;
        U scalar_u = static_cast<U>(scalar);
        for(size_t i = 0;i<3;i++)
        {
            for(size_t j = 0;j<3;j++)
            {
                res[i][j] = scalar_u + mat[i][j];
            }
        }
        return res;
    }

    template<typename T, typename U>
    Matrix3<U> operator-(T scalar, const Matrix3<U>& mat)
    {
        Matrix3<U> res;
        U scalar_u = static_cast<U>(scalar);
        for(size_t i = 0;i<3;i++)
        {
            for(size_t j = 0;j<3;j++)
            {
                res[i][j] = scalar_u - mat[i][j];
            }
        }
        return res;
    }

    template<typename T, typename U>
    Matrix3<U> operator*(T scalar, const Matrix3<U>& mat)
    {
        Matrix3<U> res;
        U scalar_u = static_cast<U>(scalar);
        for(size_t i = 0;i<3;i++)
        {
            for(size_t j = 0;j<3;j++)
            {
                res[i][j] = scalar_u * mat[i][j];
            }
        }
        return res;
    }

    template<typename T>
    Matrix3<T> Matrix3<T>::Transpose() const
    {
        Matrix3 res;
        for(size_t i =0;i<3;i++)
        {
            for(size_t j = 0;j<3;j++)
            {
                res[i][j] = data_[j][i];
            }
        }
        return res;
    }

    template<typename T>
    T Matrix3<T>::Determinant() const
    {
        T coff_00 = data_[0][0] * (data_[1][1] * data_[2][2] - data_[1][2] * data_[2][1]);
        T coff_01 = -data_[0][1] * (data_[1][0] * data_[2][2] - data_[1][2] * data_[2][0]);
        T coff_02 = data_[0][2] * (data_[1][0] * data_[2][1] - data_[1][1] * data_[2][0]);
        return coff_00 + coff_01 + coff_02;
    }

    template<typename T>
    Matrix3<T> Matrix3<T>::Adjoint() const
    {
        return Matrix3({
            data_[1][1] * data_[2][2] - data_[1][2] * data_[2][1],
            data_[0][2] * data_[2][1] - data_[0][1] * data_[2][2],
            data_[0][1] * data_[1][2] - data_[0][2] * data_[1][1],
          
            data_[1][2] * data_[2][0] - data_[1][0] * data_[2][2],
            data_[0][0] * data_[2][2] - data_[0][2] * data_[2][0],
            data_[0][2] * data_[1][0] - data_[0][0] * data_[1][2],
    
            data_[1][0] * data_[2][1] - data_[1][1] * data_[2][0],
            data_[0][1] * data_[2][0] - data_[0][0] * data_[2][1],
            data_[0][0] * data_[1][1] - data_[0][1] * data_[1][0]
        });
    }

    template<typename T>
    Matrix3<T> Matrix3<T>::Inverse() const
    {
        T det = Determinant();
        if (det == T(0))
        {
            throw std::runtime_error("determinant is zero");
        }
        return Adjoint() / det;
    }

}}

#endif