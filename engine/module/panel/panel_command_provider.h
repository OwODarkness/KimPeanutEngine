#ifndef KPENGINE_MODULE_PANEL_COMMAND_PROVIDER_H
#define KPENGINE_MODULE_PANEL_COMMAND_PROVIDER_H

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_registry.h"

namespace kpengine::panel
{
    // What the panel viewer reports about the state it owns. Runtime cannot name
    // any of it -- the loaded glyph product, the dot extent, the lit count -- so
    // the host that owns that state supplies the reader.
    struct PanelRuntimeReport final
    {
        bool glyphs_loaded = false;
        // Asset-root-relative spelling of the product actually loaded, so the
        // answer names its own fixture rather than echoing what was requested.
        std::string glyph_product;
        std::uint32_t first_codepoint = 0u;
        std::uint32_t glyph_count = 0u;
        std::uint32_t columns = 0u;
        std::uint32_t rows = 0u;
        std::uint32_t dot_width = 0u;
        std::uint32_t dot_height = 0u;
        std::uint32_t lit_dots = 0u;
        bool has_dot_mask = false;
        std::uint32_t live_gpu_handles = 0u;
        std::string text;
        // Appearance, reported for the same reason the product is: a capture
        // shows pixels and nothing else, so it cannot say which gap or colour
        // produced them. Colours are display-space "#RRGGBB".
        float dot_gap = 0.0f;
        std::string dot_color;
        std::string background_color;
        std::string accent_color;
        float gradient_amount = 0.0f;
        // "horizontal", "vertical", or "mirrored"; a word rather than a number
        // because a caller should not have to know the lane's encoding.
        std::string gradient_axis;
        float cycles_per_second = 0.0f;
    };

    // Only the fields the caller set are applied, so a command can change the gap
    // without restating the colours.
    struct PanelAppearanceUpdate final
    {
        bool has_dot_gap = false;
        float dot_gap = 0.0f;
        bool has_dot_color = false;
        std::array<float, 4> dot_color{};
        bool has_background_color = false;
        std::array<float, 4> background_color{};
        bool has_accent_color = false;
        std::array<float, 4> accent_color{};
        bool has_gradient_amount = false;
        float gradient_amount = 0.0f;
        bool has_gradient_axis = false;
        std::uint32_t gradient_axis = 0u;
        bool has_cycles_per_second = false;
        float cycles_per_second = 0.0f;
    };

    // The axis names the command accepts and the report produces, so a round trip
    // through set_appearance and report compares equal.
    std::string FormatPanelGradientAxis(std::uint32_t axis);

    // Resolved per dispatch: the renderer is created on the render thread after
    // these commands are registered. Returns false when no panel is loaded.
    using PanelReportResolver = std::function<bool(PanelRuntimeReport &)>;

    // Applies new first-row text. Returns false and fills the diagnostic when
    // the panel cannot accept it.
    using PanelSetTextResolver =
        std::function<bool(std::string_view text, std::string &diagnostic)>;

    using PanelSetAppearanceResolver =
        std::function<bool(const PanelAppearanceUpdate &update, std::string &diagnostic)>;

    struct PanelCommandResolvers final
    {
        PanelReportResolver report;
        PanelSetTextResolver set_text;
        PanelSetAppearanceResolver set_appearance;
    };

    // "#RRGGBB" in display space. Exposed rather than file-local because the
    // report and the command must spell a colour the same way, or a round trip
    // through set_appearance and report would not compare equal.
    std::string FormatPanelColor(const std::array<float, 4> &color);

    // The inverse, exposed for the same reason: the launch option and the
    // command must accept exactly the spellings the report produces.
    bool ParsePanelColor(std::string_view text, std::array<float, 4> &out);

    struct PanelCommandRegistrationResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        // Holding every token is what keeps the entries installed; the registry
        // releases them when the caller drops the result.
        std::vector<runtime::command::CommandRegistration> registrations;
    };

    // Registers panel.report, panel.set_text, and panel.set_appearance. The
    // report command exists because a capture shows pixels and nothing else: it
    // cannot say which product was loaded, how much of the panel is lit, or what
    // the dots look like, so a run that quietly fell back to a different product
    // must not read as evidence.
    PanelCommandRegistrationResult RegisterPanelCommands(
        runtime::command::CommandRegistry &registry, PanelCommandResolvers resolvers);
}

#endif
