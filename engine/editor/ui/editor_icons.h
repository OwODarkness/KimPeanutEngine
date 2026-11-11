#ifndef KPENGINE_EDITOR_ICONS_H
#define KPENGINE_EDITOR_ICONS_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "editor_ui/ui_types.h"
#include "runtime/image_io/image_io.h"

namespace kpengine::editor
{
    enum class EditorIcon : std::uint8_t
    {
        Document, Model, Material, Texture, DirectionalLight, PointLight, SpotLight,
        Application, SkipPrevious, Play, Pause, SkipNext, Stop, VolumeOn, VolumeOff,
        Repeat, Music, Count
    };

    std::string_view EditorIconName(EditorIcon icon) noexcept;

    class EditorIconCatalog final
    {
    public:
        // An empty path selects the project's icon JSON; resource paths are relative to that JSON.
        bool Load(const std::filesystem::path &configuration = {});
        image_io::ImageDecodeResult LoadImage(EditorIcon icon) const;
        image_io::ImageDecodeResult LoadImage(std::string_view name) const;
        const std::unordered_map<std::string, std::string> &Entries() const noexcept { return entries_; }
        std::string_view Diagnostic() const noexcept { return diagnostic_; }

    private:
        std::filesystem::path root_;
        std::unordered_map<std::string, std::string> entries_;
        std::string diagnostic_;
    };

    class EditorBuiltinIcons final
    {
    public:
        static constexpr std::uint32_t MaskSize = 24;

        // Returned masks borrow this catalog's top-origin CPU storage until destruction/reload.
        ui::UiIconMask Get(EditorIcon icon) const noexcept;
        std::string_view Diagnostic(EditorIcon icon) const noexcept;
        void Load(const std::filesystem::path &configuration = {});

    private:
        static constexpr std::size_t count = static_cast<std::size_t>(EditorIcon::Count);
        std::array<std::vector<std::uint8_t>, count> masks_;
        std::array<std::string, count> diagnostics_;
    };
}

#endif
