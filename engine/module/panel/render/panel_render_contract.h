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
    };

    static_assert(sizeof(PanelDrawConstants) == 32u,
                  "PanelDrawConstants must match the std140 block in panel_dots");
    static_assert(offsetof(PanelDrawConstants, background_color) == 16u,
                  "PanelDrawConstants members moved; update panel_dots to match");
}

#endif
