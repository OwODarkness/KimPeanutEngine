#ifndef KPENGINE_RUNTIME_RENDER_HEIGHT_FOG_SETTINGS_H
#define KPENGINE_RUNTIME_RENDER_HEIGHT_FOG_SETTINGS_H

#include <array>
#include <cmath>

namespace kpengine::render
{
    struct HeightFogSettings
    {
        bool enabled = false;
        bool affect_sky = false;
        float density = 0.02f;
        float reference_height = 0.0f;
        float height_falloff = 0.1f;
        float max_distance = 100.0f;
        std::array<float, 3> color{0.45f, 0.55f, 0.65f};

        bool Active() const noexcept { return enabled && density > 0.0f; }
        friend bool operator==(const HeightFogSettings &, const HeightFogSettings &) = default;
    };

    inline bool IsValidHeightFogSettings(const HeightFogSettings &s) noexcept
    {
        const auto in_range = [](float value, float low, float high) {
            return std::isfinite(value) && value >= low && value <= high;
        };
        return in_range(s.density, 0.0f, 100.0f) &&
            in_range(s.reference_height, -1000000.0f, 1000000.0f) &&
            in_range(s.height_falloff, 0.0f, 100.0f) &&
            in_range(s.max_distance, 0.001f, 1000000.0f) &&
            in_range(s.color[0], 0.0f, 65504.0f) && in_range(s.color[1], 0.0f, 65504.0f) &&
            in_range(s.color[2], 0.0f, 65504.0f);
    }
}

#endif
