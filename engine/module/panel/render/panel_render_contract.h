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

    // Mirrors the std140 block in asset/shader/panel_dots.vert and
    // panel_dots.frag. The producer writes raw bytes and the shader reads them
    // back by offset, so the layout is frozen here rather than trusted.
    struct PanelDrawConstants final
    {
        // Deliberately white on black. Both are fixed points of the sRGB
        // encode, so a capture proves the dot pattern without also depending on
        // whether the output target is sRGB or UNORM.
        std::array<float, 4> dot_color{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> background_color{0.0f, 0.0f, 0.0f, 1.0f};
        // Packed as a vec4 rather than a bare float so std140 cannot introduce a
        // padding member the two sides disagree about. x is the dot gap; the
        // rest is reserved for later appearance parameters and must stay zero.
        std::array<float, 4> params{kPanelDefaultDotGap, 0.0f, 0.0f, 0.0f};
    };

    static_assert(sizeof(PanelDrawConstants) == 48u,
                  "PanelDrawConstants must match the std140 block in panel_dots");
    static_assert(offsetof(PanelDrawConstants, background_color) == 16u,
                  "PanelDrawConstants members moved; update panel_dots to match");
    static_assert(offsetof(PanelDrawConstants, params) == 32u,
                  "PanelDrawConstants members moved; update panel_dots to match");
}

#endif
