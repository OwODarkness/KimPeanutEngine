#include "noise2d.h"

#include <algorithm>
#include <cmath>

namespace kpengine::math
{
    namespace
    {
        std::uint64_t Hash(std::int64_t x, std::int64_t y, std::uint64_t seed) noexcept
        {
            std::uint64_t value = seed ^ (static_cast<std::uint64_t>(x) * 0x9e3779b185ebca87ull) ^
                                  (static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4full);
            value ^= value >> 30;
            value *= 0xbf58476d1ce4e5b9ull;
            value ^= value >> 27;
            value *= 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        double Fade(double value) noexcept
        {
            return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
        }

        double Gradient(std::int64_t x, std::int64_t y, double dx, double dy,
                        std::uint64_t seed) noexcept
        {
            constexpr double kInvSqrt2 = 0.7071067811865475244;
            constexpr double gradients[8][2] = {
                {1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0},
                {kInvSqrt2, kInvSqrt2}, {-kInvSqrt2, kInvSqrt2},
                {kInvSqrt2, -kInvSqrt2}, {-kInvSqrt2, -kInvSqrt2}};
            const auto &gradient = gradients[Hash(x, y, seed) & 7u];
            return gradient[0] * dx + gradient[1] * dy;
        }
    }

    double PerlinNoise2D(const double x, const double y, const std::uint64_t seed) noexcept
    {
        if (!std::isfinite(x) || !std::isfinite(y)) return 0.0;
        const double floor_x = std::floor(x);
        const double floor_y = std::floor(y);
        if (floor_x < static_cast<double>(INT64_MIN) || floor_x >= static_cast<double>(INT64_MAX) ||
            floor_y < static_cast<double>(INT64_MIN) || floor_y >= static_cast<double>(INT64_MAX))
            return 0.0;
        const auto ix = static_cast<std::int64_t>(floor_x);
        const auto iy = static_cast<std::int64_t>(floor_y);
        const double dx = x - floor_x;
        const double dy = y - floor_y;
        const double u = Fade(dx);
        const double v = Fade(dy);
        const double a = Gradient(ix, iy, dx, dy, seed);
        const double b = Gradient(ix + 1, iy, dx - 1.0, dy, seed);
        const double c = Gradient(ix, iy + 1, dx, dy - 1.0, seed);
        const double d = Gradient(ix + 1, iy + 1, dx - 1.0, dy - 1.0, seed);
        const double top = a + u * (b - a);
        const double bottom = c + u * (d - c);
        return std::clamp((top + v * (bottom - top)) * 1.4142135623730951, -1.0, 1.0);
    }
}
