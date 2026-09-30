#ifndef KPENGINE_CORE_MATH_NOISE2D_H
#define KPENGINE_CORE_MATH_NOISE2D_H

#include <cstdint>

namespace kpengine::math
{
    // Stateless seeded gradient noise in continuous, caller-defined coordinates.
    double PerlinNoise2D(double x, double y, std::uint64_t seed) noexcept;
}

#endif
