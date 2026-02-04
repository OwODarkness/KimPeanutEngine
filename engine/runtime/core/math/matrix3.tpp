#include "matrix3.h"
#include "vector3.h"
#include "math.h"
namespace kpengine::math
{
    template <typename T>
    Matrix3<T>::Matrix3()
    {
        const T k_zero = T(0);
        data_[0] = {k_zero, k_zero, k_zero};
        data_[1] = {k_zero, k_zero, k_zero};
        data_[2] = {k_zero, k_zero, k_zero};
    }

    template <typename T>
    Matrix3<T>::Matrix3(const Matrix3 &m) : data_(m.data_)
    {
    }

    template <typename T>
    Matrix3<T>::Matrix3(const T (&arr)[3][3])
    {
        std::copy(arr[0], arr[0] + 3, data_[0].begin());
        std::copy(arr[1], arr[1] + 3, data_[1].begin());
        std::copy(arr[2], arr[2] + 3, data_[2].begin());
    }

    template <typename T>
    Matrix3<T>::Matrix3(T (&arr)[9])
    {
        std::copy(arr, arr + 3, data_[0].begin());
        std::copy(arr + 3, arr + 6, data_[1].begin());
        std::copy(arr + 6, arr + 9, data_[2].begin());
    }

    template <typename T>
    Matrix3<T>::Matrix3(std::initializer_list<T> list)
    {
        std::copy(list.begin(), list.begin() + 3, data_[0].begin());
        std::copy(list.begin() + 3, list.begin() + 6, data_[1].begin());
        std::copy(list.begin() + 6, list.end(), data_[2].begin());
    }

    template <typename T>
    bool Matrix3<T>::operator==(const Matrix3 &mat) const
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                if (!kpengine::math::IsNearlyEqual(data_[i][j], mat[i][j]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    template <typename T>
    bool Matrix3<T>::operator!=(const Matrix3 &mat) const
    {
        return !this->operator==(mat);
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator+(T scalar) const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j] + scalar;
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator-(T scalar) const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j] - scalar;
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator*(T scalar) const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j] * scalar;
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator/(T scalar) const
    {
        assert(scalar != T(0));

        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j] / scalar;
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator+(const Matrix3 &mat) const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j] + mat[i][j];
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator-(const Matrix3 &mat) const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[i][j] - mat[i][j];
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator-() const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = -1 * data_[i][j];
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::operator*(const Matrix3 &mat) const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t k = 0; k < 3; k++)
            {
                T sum{};
                for (size_t j = 0; j < 3; j++)
                {
                    sum += data_[i][j] * mat[j][k];
                }
                res[i][k] = sum;
            }
        }
        return res;
    }

    template <typename T>
    Vector3<T> Matrix3<T>::operator*(const Vector3<T> &v) const
    {
        Vector3<T> res;
        for (size_t i = 0; i < 3; i++)
        {
            res[i] = data_[i][0] * v[0] + data_[i][1] * v[1] + data_[i][2] * v[2];
        }
        return res;
    }

    template <typename T>
    Matrix3<T> &Matrix3<T>::operator+=(T scalar)
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                data_[i][j] += scalar;
            }
        }
        return *this;
    }

    template <typename T>
    Matrix3<T> &Matrix3<T>::operator-=(T scalar)
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                data_[i][j] -= scalar;
            }
        }

        return *this;

    }

    template <typename T>
    Matrix3<T> &Matrix3<T>::operator*=(T scalar)
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                data_[i][j] *= scalar;
            }
        }

        return *this;

    }

    template <typename T>
    Matrix3<T> &Matrix3<T>::operator/=(T scalar)
    {
        assert(scalar != T(0));

        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                data_[i][j] /= scalar;
            }
        }

        return *this;

    }


    template <typename T>
    Matrix3<T> &Matrix3<T>::operator+=(const Matrix3<T>& mat)
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                data_[i][j] += mat[i][j];
            }
        }
        return *this;

    }

    
    template <typename T>
    Matrix3<T> &Matrix3<T>::operator-=(const Matrix3& mat)
    {
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                data_[i][j] -= mat[i][j];
            }
        }
        return *this;
    }


    template <typename T, typename U>
    Matrix3<U> operator+(T scalar, const Matrix3<U> &mat)
    {
        Matrix3<U> res;
        U scalar_u = static_cast<U>(scalar);
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = scalar_u + mat[i][j];
            }
        }
        return res;
    }

    template <typename T, typename U>
    Matrix3<U> operator-(T scalar, const Matrix3<U> &mat)
    {
        Matrix3<U> res;
        U scalar_u = static_cast<U>(scalar);
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = scalar_u - mat[i][j];
            }
        }
        return res;
    }

    template <typename T, typename U>
    Matrix3<U> operator*(T scalar, const Matrix3<U> &mat)
    {
        Matrix3<U> res;
        U scalar_u = static_cast<U>(scalar);
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = scalar_u * mat[i][j];
            }
        }
        return res;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::Transpose() const
    {
        Matrix3 res;
        for (size_t i = 0; i < 3; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                res[i][j] = data_[j][i];
            }
        }
        return res;
    }

    template <typename T>
    T Matrix3<T>::Determinant() const
    {
        T coff_00 = data_[0][0] * (data_[1][1] * data_[2][2] - data_[1][2] * data_[2][1]);
        T coff_01 = -data_[0][1] * (data_[1][0] * data_[2][2] - data_[1][2] * data_[2][0]);
        T coff_02 = data_[0][2] * (data_[1][0] * data_[2][1] - data_[1][1] * data_[2][0]);
        return coff_00 + coff_01 + coff_02;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::Adjoint() const
    {
        return Matrix3({data_[1][1] * data_[2][2] - data_[1][2] * data_[2][1],
                        data_[0][2] * data_[2][1] - data_[0][1] * data_[2][2],
                        data_[0][1] * data_[1][2] - data_[0][2] * data_[1][1],

                        data_[1][2] * data_[2][0] - data_[1][0] * data_[2][2],
                        data_[0][0] * data_[2][2] - data_[0][2] * data_[2][0],
                        data_[0][2] * data_[1][0] - data_[0][0] * data_[1][2],

                        data_[1][0] * data_[2][1] - data_[1][1] * data_[2][0],
                        data_[0][1] * data_[2][0] - data_[0][0] * data_[2][1],
                        data_[0][0] * data_[1][1] - data_[0][1] * data_[1][0]});
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::Inverse() const
    {
        T det = Determinant();
        if (det == T(0))
        {
            throw std::runtime_error("determinant is zero");
        }
        return Adjoint() / det;
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::Identity()
    {
        return Matrix3{
            T(1), 0, 0,
            0, T(1), 0,
            0, 0, T(1)};
    }

    template <typename T>
    Matrix3<T> Matrix3<T>::Zero()
    {
        return Matrix3{
            0, 0, 0,
            0, 0, 0,
            0, 0, 0};
    }

}