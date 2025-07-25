#ifndef KPENGINE_RUNTIME_MATH_H
#define KPENGINE_RUNTIME_MATH_H


#include <cmath>
namespace kpengine{
namespace math{


    constexpr float Math_PI_FLOAT = 3.14159265358979323846264338327950288f;
    constexpr float Math_PI_DOUBLE = 3.14159265358979323846264338327950288f;


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
// #include <vector>
// #include <Eigen/Core>
// namespace kpengine
// {
//     namespace math
//     {
//         const std::vector<int> permutation = {151, 160, 137, 91, 90, 15,
//                                               131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23,
//                                               190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33,
//                                               88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166,
//                                               77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244,
//                                               102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196,
//                                               135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123,
//                                               5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42,
//                                               223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
//                                               129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228,
//                                               251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107,
//                                               49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254,
//                                               138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180};
//         const std::vector<Eigen::Vector3f> grads = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};

//         float PerlinNoise1D(float x);

//         float PerlinNoise2D(float x, float y);
//     }
// }

#endif