#ifndef KPENGINE_LIVE2D_RENDER_CONTRACT_H
#define KPENGINE_LIVE2D_RENDER_CONTRACT_H

#include <cstdint>
#include <string>
#include <vector>

namespace kpengine::live2d
{
    // L2D4.0 freezes a single packed RGBA mask atlas for V1. The official
    // Framework uses four channels and nine regions per channel when one
    // render texture is available.
    inline constexpr std::uint32_t kLive2DMaskAtlasWidth = 256u;
    inline constexpr std::uint32_t kLive2DMaskAtlasHeight = 256u;
    inline constexpr std::uint32_t kLive2DMaskAtlasChannelCount = 4u;
    inline constexpr std::uint32_t kLive2DMaskAtlasMaxRegionsPerChannel = 9u;
    inline constexpr std::uint32_t kLive2DMaxActiveMaskContexts =
        kLive2DMaskAtlasChannelCount * kLive2DMaskAtlasMaxRegionsPerChannel;

    // These tolerances are part of the image-comparison contract. CPU tests
    // compare linear float equations; backend tests compare normalized
    // readback channels and use the larger edge allowance only for filtered
    // mask samples.
    inline constexpr float kLive2DRenderCpuTolerance = 1.0e-5f;
    inline constexpr float kLive2DRenderBackendTolerance = 2.0f / 255.0f;
    inline constexpr float kLive2DRenderMaskEdgeTolerance = 4.0f / 255.0f;
    inline constexpr float kLive2DTransparentRgbEpsilon = 1.0e-5f;

    struct Live2DColor final
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    enum class Live2DBlendMode : std::uint8_t
    {
        Normal,
        Additive,
        Multiplicative,
    };

    // Core's compatible modes are intentionally expressed as engine policy,
    // not as Core enum values. The renderer receives straight-alpha linear
    // texture samples and returns premultiplied linear output.
    Live2DColor ShadeLive2DDrawable(Live2DColor texture_color,
                                    Live2DColor multiply_color,
                                    Live2DColor screen_color,
                                    float opacity,
                                    float mask_coverage) noexcept;

    Live2DColor CompositeLive2DColor(Live2DColor source,
                                     Live2DColor destination,
                                     Live2DBlendMode mode) noexcept;

    // The packed atlas stores the inverse mask: it is cleared to one and the
    // mask pass multiplies that channel by (1 - source alpha). The sampled
    // value is therefore converted back to drawable coverage here.
    float ResolveLive2DMaskCoverage(float stored_mask_sample,
                                    bool inverted) noexcept;

    // Ignore RGB in transparent pixels when comparing readback. This avoids
    // treating implementation-defined RGB in zero-alpha texels as visible.
    Live2DColor CanonicalizeLive2DColorForComparison(Live2DColor color) noexcept;

    enum class Live2DRenderFeatureIssue : std::uint8_t
    {
        OffscreenObjects,
        BlendGroups,
        TopologyChange,
        InvalidIndices,
        UnknownBlendMode,
        ExcessMaskContexts,
    };

    // This value-only report is populated while static model data is
    // extracted. Counts make the report machine-readable without exposing
    // Cubism/Core types or a renderer implementation.
    struct Live2DRenderFeatureReport final
    {
        std::uint32_t drawable_count = 0u;
        std::uint32_t offscreen_object_count = 0u;
        std::uint32_t blend_group_count = 0u;
        std::uint32_t invalid_index_count = 0u;
        std::uint32_t unknown_blend_mode_count = 0u;
        std::uint32_t active_mask_context_count = 0u;
        bool topology_changed = false;

        bool IsSupported() const noexcept;
    };

    struct Live2DRenderFeatureValidation final
    {
        bool supported = false;
        std::vector<Live2DRenderFeatureIssue> issues;
        std::string diagnostic;
    };

    Live2DRenderFeatureValidation ValidateLive2DRenderFeatureReport(
        const Live2DRenderFeatureReport &report);

    const char *Live2DRenderFeatureIssueName(
        Live2DRenderFeatureIssue issue) noexcept;
}

#endif
