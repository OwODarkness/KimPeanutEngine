#include "editor_icons.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <exception>
#include <nlohmann/json.hpp>

#include "runtime/core/config/path.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr std::array<std::string_view, static_cast<std::size_t>(EditorIcon::Count)> names{{
            "document", "model", "material", "texture", "directional_light", "point_light", "spot_light",
            "application", "skip_previous", "play", "pause", "skip_next", "stop", "volume_on", "volume_off",
            "repeat", "music"}};

        std::string FoldName(const std::string_view name)
        {
            std::string folded{name};
            for (char &ch : folded)
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            return folded;
        }

        bool IsRelativeResource(const std::filesystem::path &path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
            for (const auto &part : path)
                if (part == "..") return false;
            return true;
        }

        std::filesystem::path DefaultConfiguration()
        {
            for (const char *directory : {"resource/icon", "resouce/icon"})
            {
                for (const char *filename : {"setting.json", "settings.json"})
                {
                    const auto candidate = project_root / directory / filename;
                    std::error_code error;
                    if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;
                }
            }
            return {};
        }

        std::vector<std::uint8_t> BuildMask(const image_io::ImageBuffer &image)
        {
            if (!image.IsValid() || image.format != image_io::ImagePixelFormat::Rgba8)
                return {};
            std::uint32_t min_x = image.width;
            std::uint32_t min_y = image.height;
            std::uint32_t max_x = 0;
            std::uint32_t max_y = 0;
            bool visible = false;
            for (std::uint32_t y = 0; y < image.height; ++y)
            {
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    const std::size_t pixel = (static_cast<std::size_t>(y) * image.width + x) * 4 + 3;
                    if (image.pixels[pixel] < 12) continue;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                    visible = true;
                }
            }
            if (!visible) return {};

            constexpr auto size = EditorBuiltinIcons::MaskSize;
            constexpr float padded_extent = static_cast<float>(size - 2);
            const auto source_width = max_x - min_x + 1;
            const auto source_height = max_y - min_y + 1;
            const float scale = std::min(padded_extent / static_cast<float>(source_width),
                                         padded_extent / static_cast<float>(source_height));
            const auto width = std::max(1U, static_cast<std::uint32_t>(std::round(source_width * scale)));
            const auto height = std::max(1U, static_cast<std::uint32_t>(std::round(source_height * scale)));
            const auto left = (size - width) / 2;
            const auto top = (size - height) / 2;
            std::vector<std::uint8_t> mask(size * size);
            for (std::uint32_t y = 0; y < height; ++y)
            {
                // ImageIO is bottom-origin; UI masks use top-origin coordinates.
                const auto source_y = max_y - std::min(source_height - 1, y * source_height / height);
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const auto source_x = min_x + std::min(source_width - 1, x * source_width / width);
                    mask[static_cast<std::size_t>(top + y) * size + left + x] =
                        image.pixels[(static_cast<std::size_t>(source_y) * image.width + source_x) * 4 + 3];
                }
            }
            return mask;
        }
    }

    std::string_view EditorIconName(const EditorIcon icon) noexcept
    {
        const auto index = static_cast<std::size_t>(icon);
        return index < names.size() ? names[index] : std::string_view{};
    }

    bool EditorIconCatalog::Load(const std::filesystem::path &configuration)
    {
        entries_.clear();
        root_.clear();
        diagnostic_.clear();
        const auto path = configuration.empty() ? DefaultConfiguration() : configuration;
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > 256U * 1024U)
        {
            diagnostic_ = "Editor icon JSON is missing, unreadable or exceeds 256 KiB";
            return false;
        }
        try
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Could not open Editor icon JSON");
            const auto json = nlohmann::json::parse(input);
            if (!json.is_object() || !json.contains("icons") || !json.at("icons").is_object())
                throw std::runtime_error("Editor icon JSON requires an icons object");
            std::unordered_map<std::string, std::string> parsed;
            for (const auto &[name, entry] : json.at("icons").items())
            {
                if (name.empty() || !entry.is_string())
                    throw std::runtime_error("Editor icon entries require a name and relative path string");
                const auto relative = entry.get<std::string>();
                if (!IsRelativeResource(std::filesystem::path{relative}))
                    throw std::runtime_error("Editor icon paths must be relative to their JSON directory");
                if (!parsed.emplace(FoldName(name), relative).second)
                    throw std::runtime_error("Editor icon names must be unique ignoring ASCII case");
            }
            root_ = std::filesystem::absolute(path).parent_path();
            entries_ = std::move(parsed);
            return true;
        }
        catch (const std::exception &exception)
        {
            diagnostic_ = std::string{"Invalid Editor icon JSON: "} + exception.what();
            return false;
        }
    }

    image_io::ImageDecodeResult EditorIconCatalog::LoadImage(const EditorIcon icon) const
    {
        return LoadImage(EditorIconName(icon));
    }

    image_io::ImageDecodeResult EditorIconCatalog::LoadImage(const std::string_view name) const
    {
        if (!diagnostic_.empty()) return {{false, diagnostic_}, {}};
        const auto found = entries_.find(FoldName(name));
        if (found == entries_.end())
            return {{false, "Editor icon is not configured: " + std::string{name}}, {}};
        return image_io::DecodeImageFile((root_ / found->second).generic_string());
    }

    ui::UiIconMask EditorBuiltinIcons::Get(const EditorIcon icon) const noexcept
    {
        const auto index = static_cast<std::size_t>(icon);
        if (index >= masks_.size() || masks_[index].empty()) return {};
        return {masks_[index], MaskSize, MaskSize};
    }

    std::string_view EditorBuiltinIcons::Diagnostic(const EditorIcon icon) const noexcept
    {
        const auto index = static_cast<std::size_t>(icon);
        return index < diagnostics_.size() ? diagnostics_[index] : "Invalid Editor icon";
    }

    void EditorBuiltinIcons::Load(const std::filesystem::path &configuration)
    {
        EditorIconCatalog catalog;
        (void)catalog.Load(configuration);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto decoded = catalog.LoadImage(static_cast<EditorIcon>(index));
            masks_[index] = decoded.result.success ? BuildMask(decoded.image) : std::vector<std::uint8_t>{};
            diagnostics_[index] = decoded.result.success
                ? (masks_[index].empty() ? "Editor icon has no visible RGBA mask" : "")
                : decoded.result.diagnostic;
        }
    }
}
