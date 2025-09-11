#ifndef KPENGINE_RUNTIME_MATH_H
#define KPENGINE_RUNTIME_MATH_H


#include <cmath>
namespace kpengine{
namespace math{

    constexpr float Math_PI_FLOAT = 3.14159265358979323846264338327950288f;
    constexpr double Math_PI_DOUBLE = 3.14159265358979323846264338327950288;


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

    inline double Lerp(double a, double b, double alpha)
    {
        return (1.f - alpha) * a + alpha * b;
    }

    inline float Lerp(float a, float b, float alpha)
    {
        return (1.f - alpha) * a + alpha * b;
    }

    inline float DegreeToRadian(float degree)
    {
        return degree * Math_PI_FLOAT / 180.f;
    }

    inline double DegreeToRadian(double degree)
    {
        return degree * Math_PI_DOUBLE / 180.0;
    }

    inline float RadianToDegree(float radian)
    {
        return radian * 180.f / Math_PI_FLOAT;
    }

    inline double RadianToDegree(double radian)
    {
        return radian * 180.0 / Math_PI_DOUBLE;
    }

}
}


#endif