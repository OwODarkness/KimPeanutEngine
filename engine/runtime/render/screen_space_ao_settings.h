#ifndef KPENGINE_RUNTIME_RENDER_SCREEN_SPACE_AO_SETTINGS_H
#define KPENGINE_RUNTIME_RENDER_SCREEN_SPACE_AO_SETTINGS_H

#include <cstdint>

namespace kpengine::render
{
    enum class ScreenSpaceAoQuality : std::uint8_t { Low, Medium, High };

    constexpr std::uint32_t ScreenSpaceAoSampleCount(ScreenSpaceAoQuality quality) noexcept
    {
        switch (quality)
        {
        case ScreenSpaceAoQuality::Low: return 6u;
        case ScreenSpaceAoQuality::Medium: return 12u;
        case ScreenSpaceAoQuality::High: return 24u;
        }
        return 12u;
    }

    struct ScreenSpaceAoSettings
    {
        bool enabled = true;
        float radius = 0.65f;
        float bias = 0.025f;
        float strength = 1.0f;
        ScreenSpaceAoQuality quality = ScreenSpaceAoQuality::Medium;
    };
}

#endif
