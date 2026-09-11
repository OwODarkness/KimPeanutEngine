#ifndef KPENGINE_LIVE2D_SETTINGS_H
#define KPENGINE_LIVE2D_SETTINGS_H

#include <array>
#include <string>

namespace kpengine::live2d
{
    struct Live2DSettings final
    {
        int version = 1;
        bool enabled = false;
        std::string preview_asset;
    };

    Live2DSettings ReadLive2DSettings(const std::string &path);

    // Reads the shared display-space background used by standalone viewer
    // presentation. The fallback preserves the backend's historical Vulkan
    // clear when the optional application settings file is unavailable.
    std::array<float, 4> ReadWindowBackgroundColor(const std::string &path);
}

#endif // KPENGINE_LIVE2D_SETTINGS_H
