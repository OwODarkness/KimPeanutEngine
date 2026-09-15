#ifndef KPENGINE_MODULE_PANEL_RENDER_CONTRACT_H
#define KPENGINE_MODULE_PANEL_RENDER_CONTRACT_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace kpengine::panel
{
    // Descriptor bindings the panel pipeline declares. The submission contract
    // currently accepts set zero only, so these are bindings rather than pairs.
    inline constexpr std::uint32_t kPanelConstantsBinding = 0u;
    inline constexpr std::uint32_t kPanelDotMaskBinding = 1u;

    // One quad, four vertices, two triangles.
    inline constexpr std::uint32_t kPanelQuadVertexCount = 4u;
    inline constexpr std::uint32_t kPanelQuadIndexCount = 6u;

    // How much of a dot is left dark on every side. Without it adjacent lit dots
    // merge into solid runs and the panel reads as a low-resolution image rather
    // than as a matrix of elements; with it each lit dot is its own square.
    //
    // The gap is a fraction of a dot, not a pixel count, so the bezel is
    // resolution independent: the same panel looks the same at any zoom or
    // viewing distance, which is what a scene-placed panel will need.
    inline constexpr float kPanelDefaultDotGap = 0.25f;

    // Which direction the colour ramp runs across the panel. Mirrored runs
    // outward from the centre, so the two halves agree.
    enum class PanelGradientAxis : std::uint32_t
    {
        Horizontal = 0u,
        Vertical = 1u,
        Mirrored = 2u,
    };

    // Mirrors the std140 block in asset/shader/panel_dots.vert and
    // panel_dots.frag. The producer writes raw bytes and the shader reads them
    // back by offset, so the layout is frozen here rather than trusted.
    //
    // Every field is a vec4 with one meaning per lane. Packing two parameters
    // into one lane would save nothing -- std140 pads anyway -- and would make
    // the two sides' agreement harder to check.
    struct PanelDrawConstants final
    {
        // Linear, not display space: the planner linearises what a caller picks.
        // The defaults are deliberately white on black, because both are fixed
        // points of the sRGB encode, so a capture proves the dot pattern without
        // also depending on whether the output target is sRGB or UNORM.
        std::array<float, 4> dot_color{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> background_color{0.0f, 0.0f, 0.0f, 1.0f};
        // The far end of the colour ramp. Unused while params.y is zero, which
        // is the default, so an unset gradient changes nothing.
        std::array<float, 4> accent_color{1.0f, 1.0f, 1.0f, 1.0f};
        // x dot gap, y gradient amount 0..1, z gradient axis, w reserved.
        std::array<float, 4> params{kPanelDefaultDotGap, 0.0f, 0.0f, 0.0f};
        // x elapsed seconds, y cycles per second, z and w reserved. Zero cycles
        // per second is a still ramp.
        std::array<float, 4> motion{0.0f, 0.0f, 0.0f, 0.0f};
        // The lit extent in normalized panel coordinates, as min_x/min_y/max_x/
        // max_y. The ramp spans *this* rather than the whole panel: a short line
        // of text occupies a fraction of a wide panel, so a ramp across the panel
        // moves the colour only imperceptibly over the text it is meant to
        // colour. The default is the whole panel, which is what a caller that
        // has not measured the content should get.
        std::array<float, 4> ink_bounds{0.0f, 0.0f, 1.0f, 1.0f};
    };

    static_assert(sizeof(PanelDrawConstants) == 96u,
                  "PanelDrawConstants must match the std140 block in panel_dots");
    static_assert(offsetof(PanelDrawConstants, background_color) == 16u,
                  "PanelDrawConstants members moved; update panel_dots to match");
    static_assert(offsetof(PanelDrawConstants, accent_color) == 32u,
                  "PanelDrawConstants members moved; update panel_dots to match");
    static_assert(offsetof(PanelDrawConstants, params) == 48u,
                  "PanelDrawConstants members moved; update panel_dots to match");
    static_assert(offsetof(PanelDrawConstants, motion) == 64u,
                  "PanelDrawConstants members moved; update panel_dots to match");
    static_assert(offsetof(PanelDrawConstants, ink_bounds) == 80u,
                  "PanelDrawConstants members moved; update panel_dots to match");
}

#endif
