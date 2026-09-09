#include "live2d_render_contract.h"

#include <algorithm>

namespace kpengine::live2d
{
    namespace
    {
        float Clamp01(const float value) noexcept
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        Live2DColor ClampColor(const Live2DColor color) noexcept
        {
            return {Clamp01(color.r), Clamp01(color.g), Clamp01(color.b),
                    Clamp01(color.a)};
        }

        const char *DiagnosticForIssue(const Live2DRenderFeatureIssue issue)
            noexcept
        {
            switch (issue)
            {
            case Live2DRenderFeatureIssue::OffscreenObjects:
                return "Live2D render feature unsupported: offscreen objects are not supported in V1";
            case Live2DRenderFeatureIssue::BlendGroups:
                return "Live2D render feature unsupported: blend groups are not supported in V1";
            case Live2DRenderFeatureIssue::TopologyChange:
                return "Live2D render feature unsupported: topology changed after static extraction";
            case Live2DRenderFeatureIssue::InvalidIndices:
                return "Live2D render feature invalid: drawable index data contains an invalid index";
            case Live2DRenderFeatureIssue::UnknownBlendMode:
                return "Live2D render feature unsupported: drawable uses an unknown blend mode";
            case Live2DRenderFeatureIssue::ExcessMaskContexts:
                return "Live2D render feature unsupported: active mask context count exceeds V1 capacity";
            }
            return "Live2D render feature unsupported: unknown feature issue";
        }
    }

    Live2DColor ShadeLive2DDrawable(const Live2DColor texture_color,
                                    const Live2DColor multiply_color,
                                    const Live2DColor screen_color,
                                    const float opacity,
                                    const float mask_coverage) noexcept
    {
        // Match the R5 straight-alpha shader: multiply first, then screen.
        const float transformed_r = texture_color.r * multiply_color.r;
        const float transformed_g = texture_color.g * multiply_color.g;
        const float transformed_b = texture_color.b * multiply_color.b;
        const Live2DColor straight_color{
            transformed_r + screen_color.r - transformed_r * screen_color.r,
            transformed_g + screen_color.g - transformed_g * screen_color.g,
            transformed_b + screen_color.b - transformed_b * screen_color.b,
            texture_color.a,
        };

        const float effective_alpha =
            straight_color.a * Clamp01(opacity) * Clamp01(mask_coverage);
        return {straight_color.r * effective_alpha,
                straight_color.g * effective_alpha,
                straight_color.b * effective_alpha,
                effective_alpha};
    }

    Live2DColor CompositeLive2DColor(const Live2DColor source,
                                     const Live2DColor destination,
                                     const Live2DBlendMode mode) noexcept
    {
        Live2DColor result{};
        switch (mode)
        {
        case Live2DBlendMode::Normal:
        {
            const float destination_factor = 1.0f - source.a;
            result = {source.r + destination.r * destination_factor,
                      source.g + destination.g * destination_factor,
                      source.b + destination.b * destination_factor,
                      source.a + destination.a * destination_factor};
            break;
        }
        case Live2DBlendMode::Additive:
            // R5 compatible additive blending uses (ONE, ONE) for RGB and
            // (ZERO, ONE) for alpha.
            result = {source.r + destination.r, source.g + destination.g,
                      source.b + destination.b, destination.a};
            break;
        case Live2DBlendMode::Multiplicative:
            // R5 compatible multiplicative blending uses (DST_COLOR,
            // ONE_MINUS_SRC_ALPHA) for RGB and (ZERO, ONE) for alpha.
            result = {source.r * destination.r +
                          destination.r * (1.0f - source.a),
                      source.g * destination.g +
                          destination.g * (1.0f - source.a),
                      source.b * destination.b +
                          destination.b * (1.0f - source.a),
                      destination.a};
            break;
        }
        return ClampColor(result);
    }

    float ResolveLive2DMaskCoverage(const float stored_mask_sample,
                                    const bool inverted) noexcept
    {
        const float stored = Clamp01(stored_mask_sample);
        return inverted ? stored : 1.0f - stored;
    }

    Live2DColor CanonicalizeLive2DColorForComparison(
        Live2DColor color) noexcept
    {
        if (color.a <= kLive2DTransparentRgbEpsilon)
        {
            color.r = 0.0f;
            color.g = 0.0f;
            color.b = 0.0f;
        }
        return color;
    }

    bool Live2DRenderFeatureReport::IsSupported() const noexcept
    {
        return offscreen_object_count == 0u && blend_group_count == 0u &&
               invalid_index_count == 0u && unknown_blend_mode_count == 0u &&
               !topology_changed &&
               active_mask_context_count <= kLive2DMaxActiveMaskContexts;
    }

    Live2DRenderFeatureValidation ValidateLive2DRenderFeatureReport(
        const Live2DRenderFeatureReport &report)
    {
        Live2DRenderFeatureValidation validation{};
        validation.supported = report.IsSupported();

        const auto add_issue = [&validation](
                                   const Live2DRenderFeatureIssue issue) {
            validation.issues.push_back(issue);
            if (validation.diagnostic.empty())
            {
                validation.diagnostic = DiagnosticForIssue(issue);
            }
        };

        if (report.offscreen_object_count != 0u)
        {
            add_issue(Live2DRenderFeatureIssue::OffscreenObjects);
        }
        if (report.blend_group_count != 0u)
        {
            add_issue(Live2DRenderFeatureIssue::BlendGroups);
        }
        if (report.topology_changed)
        {
            add_issue(Live2DRenderFeatureIssue::TopologyChange);
        }
        if (report.invalid_index_count != 0u)
        {
            add_issue(Live2DRenderFeatureIssue::InvalidIndices);
        }
        if (report.unknown_blend_mode_count != 0u)
        {
            add_issue(Live2DRenderFeatureIssue::UnknownBlendMode);
        }
        if (report.active_mask_context_count > kLive2DMaxActiveMaskContexts)
        {
            add_issue(Live2DRenderFeatureIssue::ExcessMaskContexts);
        }
        return validation;
    }

    const char *Live2DRenderFeatureIssueName(
        const Live2DRenderFeatureIssue issue) noexcept
    {
        switch (issue)
        {
        case Live2DRenderFeatureIssue::OffscreenObjects:
            return "offscreen_objects";
        case Live2DRenderFeatureIssue::BlendGroups:
            return "blend_groups";
        case Live2DRenderFeatureIssue::TopologyChange:
            return "topology_change";
        case Live2DRenderFeatureIssue::InvalidIndices:
            return "invalid_indices";
        case Live2DRenderFeatureIssue::UnknownBlendMode:
            return "unknown_blend_mode";
        case Live2DRenderFeatureIssue::ExcessMaskContexts:
            return "excess_mask_contexts";
        }
        return "unknown";
    }
}
