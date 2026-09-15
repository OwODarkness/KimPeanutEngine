#ifndef KPENGINE_RUNTIME_CORE_BASE_COLOR_H
#define KPENGINE_RUNTIME_CORE_BASE_COLOR_H

#include <cmath>

namespace kpengine
{
    // The sRGB transfer function and its inverse.
    //
    // They live here because three separate modules needed them and each had
    // grown its own copy: a colour a person picks is in display space, while
    // every sRGB render target the hardware encodes on store expects linear, and
    // both directions of that conversion are easy to get subtly wrong.
    //
    // White and black are fixed points of both, which is why a default of pure
    // white or black survives a round trip unchanged.
    inline float SrgbToLinear(const float value) noexcept
    {
        return value <= 0.04045f ? value / 12.92f
                                 : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    inline float LinearToSrgb(const float value) noexcept
    {
        const float clamped = value < 0.0f ? 0.0f : value;
        return clamped <= 0.0031308f ? clamped * 12.92f
                                     : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
    }
}

#endif
