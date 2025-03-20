#ifndef KPENGINE_RUNTIME_MATH_VECTOR3_H
#define KPENGINE_RUNTIME_MATH_VECTOR3_H

#include <cstddef>
#include <cassert>

#include "math.h"

namespace kpengine{
    namespace math{
        template<typename T>
        class Vector3{
        public:
            Vector3():x_{}, y_{}, z_{}{}
            explicit Vector3(T value):x_(value), y_(value), z_(value){}
            Vector3(T x, T y, T z):x_(x), y_(y), z_(z){}
            explicit Vector3(const T arr[3]):x_(arr[0]), y_(arr[1]), z_(arr[2]){}

            const T operator[](size_t index) const
            {
                assert(index < 3);
                return *(&this->x_ + index);
            }

            T& operator[](size_t index)
            {
                assert(index < 3);
                return *(&this->x_ + index);
            }

            T* Data() {return &x_;}
            const T* Data() const{return &x_;}

            inline double SquareLength() const{return x_ * x_ + y_ * y_ + z_ * z_;}
            double Length() const {return std::sqrt(SquareLength());}

            bool operator==(const Vector3& v) const {return x_ == v.x_ && y_ == v.y_ && z_ == v.z_;}
            bool operator!=(const Vector3& v) const {return x_ != v.x_ || y_ != v.y_ || z_ != v.z_;}
            
            Vector3 operator+(const Vector3& v) noexcept{return Vector3(x_ + v.x_, y_ + v.y_, z_ + v.z_);}
            Vector3 operator-(const Vector3& v) noexcept{return Vector3(x_ - v.x_, y_ - v.y_, z_ - v.z_);}
            Vector3 operator*(const Vector3& v) noexcept{return Vector3(x_ * v.x_, y_ * v.y_, z_ * v.z_);}
            Vector3 operator+(T scalar) noexcept{return Vector3(x_ + scalar, y_ + scalar, z_ + scalar);}
            Vector3 operator-(T scalar) noexcept{return Vector3(x_ - scalar, y_ - scalar, z_ - scalar);}
            Vector3 operator*(T scalar) noexcept{return Vector3(x_ *scalar, y_ * scalar, z_ * scalar);}
            Vector3 operator/(T scalar){
                assert(scalar != T(0));
                return Vector3(x_ / scalar, y_ / scalar, z_ / scalar);
            }
            Vector3 operator-(){return Vector3(-x_, -y_, -z_);}

            Vector3& operator+=(T scalar);
            Vector3& operator-=(T scalar);
            Vector3& operator*=(T scalar);
            Vector3& operator/=(T scalar);

            Vector3& operator+=(const Vector3& v);
            Vector3& operator-=(const Vector3& v);
            Vector3& operator*=(const Vector3& v);
            Vector3& operator/=(const Vector3& v);

            T DotProduct(const Vector3& v) const;
            Vector3 CrossProduct(const Vector3& v) const;
            Vector3 Reflect(const Vector3& normal) const;
            Vector3 GetSafetyNormalize() const;
            void Normalize();
            void SafetyNormalize();
            template<typename U>
            friend Vector3<U> operator+(U scalar, const Vector3<U>& v);

            template<typename U, typename V>
            friend Vector3<V> operator+(U scalar, const Vector3<V>& v);

            template<typename U>
            friend Vector3<U> operator-(U scalar, const Vector3<U>& v);

            template<typename U, typename V>
            friend Vector3<V> operator-(U scalar, const Vector3<V>& v);

            template<typename U>
            friend Vector3<U> operator*(U scalar, const Vector3<U>& v);

            template<typename U, typename V>
            friend Vector3<V> operator*(U scalar, const Vector3<V>& v);

        public:
            T x_, y_, z_;
        };


        template<typename T>
        Vector3<T>& Vector3<T>::operator+=(T scalar)
        {
            x_ += scalar;
            y_ += scalar;
            z_ += scalar;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator-=(T scalar)
        {
            x_ -= scalar;
            y_ -= scalar;
            z_ -= scalar;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator*=(T scalar)
        {
            x_ *= scalar;
            y_ *= scalar;
            z_ *= scalar;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator/=(T scalar)
        {
            assert(scalar != T(0));
            x_ /= scalar;
            y_ /= scalar;
            z_ /= scalar;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator+=(const Vector3& v)
        {
            x_ += v.x_;
            y_ += v.y_;
            z_ += v.z_;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator-=(const Vector3& v)
        {
            x_ -= v.x_;
            y_ -= v.y_;
            z_ -= v.z_;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator*=(const Vector3& v)
        {
            x_ *= v.x_;
            y_ *= v.y_;
            z_ *= v.z_;
            return *this;
        }

        template<typename T>
        Vector3<T>& Vector3<T>::operator/=(const Vector3& v)
        {
            assert(v.x_ != T(0) && v.y_ != T(0) && v.z_ != T(0));
            x_ /= v.x_;
            y_ /= v.y_;
            z_ /= v.z_;
            return *this;
        }

        template<typename T>
        Vector3<T> operator+(T scalar, const Vector3<T>& v)
        {
            return Vector3<T>(v.x_ + scalar, v.y_ + scalar, v.z_ + scalar);
        }

        template<typename T, typename U>
        Vector3<U> operator+(T scalar, const Vector3<U>& v)
        {
            U scalar_u = static_cast<U>(scalar);
            return Vector3<U>(
                scalar_u + v.x_, 
                scalar_u + v.y_, 
                scalar_u + v.z_);
        }

        template<typename T>
        Vector3<T> operator-(T scalar, const Vector3<T>& v)
        {
            return Vector3<T>(scalar - v.x_, scalar - v.y_, scalar - v.z_);
        }

        template<typename T, typename U>
        Vector3<U> operator-(T scalar, const Vector3<U>& v)
        {
            U scalar_u = static_cast<U>(scalar);
            return Vector3<U>(
                scalar_u - v.x_, 
                scalar_u - v.y_, 
                scalar_u - v.z_);
        }


        template<typename T>
        Vector3<T> operator*(T scalar, const Vector3<T>& v)
        {
            return Vector3<T>(v.x_ * scalar, v.y_ * scalar, v.z_ * scalar);
        }

        template<typename T, typename U>
        Vector3<U> operator*(T scalar, const Vector3<U>& v)
        {
            U scalar_u = static_cast<U>(scalar);
            return Vector3<U>(
                scalar_u * v.x_, 
                scalar_u * v.y_, 
                scalar_u * v.z_);
        }

        template<typename T>
        T Vector3<T>::DotProduct(const Vector3& v) const
        {
            return x_ * v.x_ + y_ * v.y_ + z_ * v.z_;
        }

        template<typename T>
        Vector3<T> Vector3<T>::CrossProduct(const Vector3& v) const
        {
            return Vector3(
                y_ * v.z_ - z_ * v.y_,
                z_ * v.x_ - x_ * v.z_,
                x_ * v.y_ - y_ * v.x_
            );
        }

        template<typename T>
        Vector3<T> Vector3<T>::Reflect(const Vector3& Normal) const
        {
            return this - (2 * this->DotProduct(Normal) * Normal);
        }

        template<typename T>
        void Vector3<T>::Normalize()
        {
            double length = Length();
            if(length == 0.)
            {
                return;
            }
            T coff = T(1.0/length);
            x_ *= coff;
            y_ *= coff;
            z_ *= coff;
        }

        template<typename T>
        void Vector3<T>::SafetyNormalize()
        {
            double length = Length();
            if(length == 0.)
            {
                length += 1e-4;
                return;
            }
            T coff = T(1.0/length);
            x_ *= coff;
            y_ *= coff;
            z_ *= coff;
        }

        template<typename T>
        Vector3<T> Vector3<T>::GetSafetyNormalize() const
        {
            double length = Length();
            if(length == 0.)
            {
                length += 1e-4;
            }
            T coff = T(1.0/length);
            return Vector3f(coff * x_, coff * y_, coff * z_);
        }
        

    }
}

#endif