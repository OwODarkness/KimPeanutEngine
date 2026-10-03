#include "audio_transport_icons.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include "runtime/core/config/path.h"
#include "runtime/image_io/image_io.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr std::array<std::string_view,
            static_cast<std::size_t>(AudioTransportIcon::Count)> kIconFiles{{
                "skip-previous.png", "play.png", "pause.png", "skipnext.png",
                "stop.png", "voice_open.png", "voice_close.png", "loop.png"}};

        std::vector<std::uint8_t> LoadIconMask(const std::string_view filename)
        {
            const std::filesystem::path path = project_root / "resouce" / "icon" /
                "audio" / std::string{filename};
            const image_io::ImageDecodeResult decoded =
                image_io::DecodeImageFile(path.generic_string());
            if (!decoded.result.success || !decoded.image.IsValid() ||
                decoded.image.format != image_io::ImagePixelFormat::Rgba8)
                return {};

            std::uint32_t min_x = decoded.image.width;
            std::uint32_t min_y = decoded.image.height;
            std::uint32_t max_x = 0;
            std::uint32_t max_y = 0;
            bool has_opaque_pixel = false;
            for (std::uint32_t y = 0; y < decoded.image.height; ++y)
            {
                for (std::uint32_t x = 0; x < decoded.image.width; ++x)
                {
                    const std::size_t pixel =
                        (static_cast<std::size_t>(y) * decoded.image.width + x) * 4 + 3;
                    if (decoded.image.pixels[pixel] < 12)
                        continue;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                    has_opaque_pixel = true;
                }
            }
            if (!has_opaque_pixel)
                return {};

            const std::uint32_t source_width = max_x - min_x + 1;
            const std::uint32_t source_height = max_y - min_y + 1;
            constexpr float padding = 1.0f;
            constexpr std::uint32_t icon_size = AudioTransportIconMasks::Size;
            const float scale = std::min(
                (icon_size - padding * 2.0f) / static_cast<float>(source_width),
                (icon_size - padding * 2.0f) / static_cast<float>(source_height));
            const auto target_width = std::max(1u, static_cast<std::uint32_t>(
                std::round(source_width * scale)));
            const auto target_height = std::max(1u, static_cast<std::uint32_t>(
                std::round(source_height * scale)));
            const std::uint32_t target_x = (icon_size - target_width) / 2;
            const std::uint32_t target_y = (icon_size - target_height) / 2;
            std::vector<std::uint8_t> alpha(icon_size * icon_size);
            for (std::uint32_t y = 0; y < target_height; ++y)
            {
                const std::uint32_t source_y = max_y - std::min(
                    source_height - 1, y * source_height / target_height);
                for (std::uint32_t x = 0; x < target_width; ++x)
                {
                    const std::uint32_t source_x = min_x + std::min(
                        source_width - 1, x * source_width / target_width);
                    const std::size_t source_pixel =
                        (static_cast<std::size_t>(source_y) * decoded.image.width +
                         source_x) * 4 + 3;
                    alpha[static_cast<std::size_t>(target_y + y) * icon_size +
                          target_x + x] = decoded.image.pixels[source_pixel];
                }
            }
            return alpha;
        }
    }

    EditorControlIcon AudioTransportIconMasks::Get(const AudioTransportIcon icon) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(icon);
        if (index >= alpha.size() || alpha[index].empty())
            return {};
        return {alpha[index], Size, Size};
    }

    TransportStripIcons AudioTransportIconMasks::GetTransportIcons() const noexcept
    {
        return {
            .previous = Get(AudioTransportIcon::Previous),
            .play = Get(AudioTransportIcon::Play),
            .pause = Get(AudioTransportIcon::Pause),
            .next = Get(AudioTransportIcon::Next),
            .stop = Get(AudioTransportIcon::Stop),
            .loop = Get(AudioTransportIcon::Loop)};
    }

    AudioTransportIconMasks LoadAudioTransportIconMasks()
    {
        AudioTransportIconMasks masks{};
        for (std::size_t i = 0; i < masks.alpha.size(); ++i)
            masks.alpha[i] = LoadIconMask(kIconFiles[i]);
        return masks;
    }
}
