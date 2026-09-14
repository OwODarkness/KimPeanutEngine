#ifndef KPENGINE_RUNTIME_MATH_H
#define KPENGINE_RUNTIME_MATH_H


#include <cmath>
#include <concepts>
namespace kpengine{
namespace math{

    inline constexpr float Math_PI_FLOAT = 3.14159265358979323846264338327950288f;
    inline constexpr double Math_PI_DOUBLE = 3.14159265358979323846264338327950288;


    inline bool IsNearlyZero(float value, float tolerance = 1e-6)
    {
        return std::fabs(value) < tolerance;
    }

    inline bool IsNearlyZero(double value, double tolerance = 1e-6)
    {
        return std::fabs(value) < tolerance;
    }

    inline bool IsNearlyEqual(float a, float b, float tolerance = 1e-6)
    {
        return std::fabs(a - b) < tolerance;
    }

    inline bool IsNearlyEqual(double a, double b, double tolerance = 1e-6)
    {
        return std::fabs(a - b) < tolerance;
    }

    template <std::floating_point T>
    constexpr T Lerp(T a, T b, T alpha) noexcept
    {
        return (T(1) - alpha) * a + alpha * b;
    }

    template <std::floating_point T>
    constexpr T DegreeToRadian(T degree) noexcept
    {
        constexpr T pi = static_cast<T>(Math_PI_DOUBLE);
        return degree * pi / T(180);
    }

    template <std::floating_point T>
    constexpr T RadianToDegree(T radian) noexcept
    {
        constexpr T pi = static_cast<T>(Math_PI_DOUBLE);
        return radian * T(180) / pi;
    }

}
}


#endif
