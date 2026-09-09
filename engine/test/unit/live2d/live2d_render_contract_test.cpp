#include <array>
#include <gtest/gtest.h>

#include "live2d_render_contract.h"

namespace kpengine::live2d
{
    namespace
    {
        void ExpectColorNear(const Live2DColor expected,
                             const Live2DColor actual)
        {
            EXPECT_NEAR(expected.r, actual.r, kLive2DRenderCpuTolerance);
            EXPECT_NEAR(expected.g, actual.g, kLive2DRenderCpuTolerance);
            EXPECT_NEAR(expected.b, actual.b, kLive2DRenderCpuTolerance);
            EXPECT_NEAR(expected.a, actual.a, kLive2DRenderCpuTolerance);
        }
    }

    TEST(Live2DRenderContractTest, AppliesStraightColorModifiersAndPmaOutput)
    {
        struct ShadeCase final
        {
            const char *name;
            Live2DColor texture;
            Live2DColor multiply;
            Live2DColor screen;
            float opacity;
            float mask;
            Live2DColor expected;
        };

        const std::array<ShadeCase, 2> cases{{
            {"zero_alpha", {0.8f, 0.2f, 0.4f, 0.0f},
             {0.5f, 0.25f, 0.75f, 1.0f}, {0.2f, 0.3f, 0.4f, 1.0f}, 0.5f,
             1.0f, {0.0f, 0.0f, 0.0f, 0.0f}},
            {"partial_alpha_with_modifiers", {0.8f, 0.4f, 0.2f, 0.5f},
             {0.5f, 0.25f, 0.75f, 1.0f}, {0.1f, 0.2f, 0.3f, 1.0f}, 0.8f,
             0.5f, {0.092f, 0.056f, 0.081f, 0.2f}},
        }};

        for (const ShadeCase &test_case : cases)
        {
            SCOPED_TRACE(test_case.name);
            const Live2DColor actual = ShadeLive2DDrawable(
                test_case.texture, test_case.multiply, test_case.screen,
                test_case.opacity, test_case.mask);
            ExpectColorNear(test_case.expected, actual);
        }
    }

    TEST(Live2DRenderContractTest, MatchesEveryV1BlendEquation)
    {
        const Live2DColor destination{0.7f, 0.1f, 0.2f, 0.3f};
        const Live2DColor source = ShadeLive2DDrawable(
            {0.6f, 0.2f, 0.8f, 0.5f}, {1.0f, 1.0f, 1.0f, 1.0f},
            {0.0f, 0.0f, 0.0f, 1.0f}, 0.5f, 1.0f);

        struct BlendCase final
        {
            Live2DBlendMode mode;
            Live2DColor expected;
        };
        const std::array<BlendCase, 3> cases{{
            {Live2DBlendMode::Normal, {0.675f, 0.125f, 0.35f, 0.475f}},
            {Live2DBlendMode::Additive, {0.85f, 0.15f, 0.4f, 0.3f}},
            {Live2DBlendMode::Multiplicative,
             {0.63f, 0.08f, 0.19f, 0.3f}},
        }};

        for (const BlendCase &test_case : cases)
        {
            ExpectColorNear(test_case.expected,
                            CompositeLive2DColor(source, destination,
                                                 test_case.mode));
        }
    }

    TEST(Live2DRenderContractTest, ResolvesOrdinaryAndInvertedMasks)
    {
        EXPECT_FLOAT_EQ(0.75f, ResolveLive2DMaskCoverage(0.25f, false));
        EXPECT_FLOAT_EQ(0.25f, ResolveLive2DMaskCoverage(0.25f, true));
        EXPECT_FLOAT_EQ(0.0f, ResolveLive2DMaskCoverage(2.0f, false));
        EXPECT_FLOAT_EQ(1.0f, ResolveLive2DMaskCoverage(-1.0f, false));
    }

    TEST(Live2DRenderContractTest, CanonicalizesTransparentRgb)
    {
        const Live2DColor canonical =
            CanonicalizeLive2DColorForComparison({0.8f, 0.4f, 0.2f, 0.0f});
        ExpectColorNear({0.0f, 0.0f, 0.0f, 0.0f}, canonical);
        ExpectColorNear({0.8f, 0.4f, 0.2f, 0.5f},
                        CanonicalizeLive2DColorForComparison(
                            {0.8f, 0.4f, 0.2f, 0.5f}));
    }

    TEST(Live2DRenderContractTest, ReportsSupportedPackedClippingAtCapacity)
    {
        const Live2DRenderFeatureReport report{
            12u, 0u, 0u, 0u, 0u, kLive2DMaxActiveMaskContexts, false};
        const Live2DRenderFeatureValidation validation =
            ValidateLive2DRenderFeatureReport(report);

        EXPECT_TRUE(report.IsSupported());
        EXPECT_TRUE(validation.supported);
        EXPECT_TRUE(validation.issues.empty());
        EXPECT_TRUE(validation.diagnostic.empty());
    }

    TEST(Live2DRenderContractTest, ReportsEachUnsupportedFeatureWithStableName)
    {
        struct FeatureCase final
        {
            const char *name;
            const char *issue_name;
            Live2DRenderFeatureReport report;
            Live2DRenderFeatureIssue issue;
            const char *diagnostic;
        };

        const std::array<FeatureCase, 6> cases{{
            {"offscreen", "offscreen_objects",
             {1u, 1u, 0u, 0u, 0u, 0u, false},
             Live2DRenderFeatureIssue::OffscreenObjects,
             "Live2D render feature unsupported: offscreen objects are not supported in V1"},
            {"blend_group", "blend_groups",
             {1u, 0u, 1u, 0u, 0u, 0u, false},
             Live2DRenderFeatureIssue::BlendGroups,
             "Live2D render feature unsupported: blend groups are not supported in V1"},
            {"topology", "topology_change",
             {1u, 0u, 0u, 0u, 0u, 0u, true},
             Live2DRenderFeatureIssue::TopologyChange,
             "Live2D render feature unsupported: topology changed after static extraction"},
            {"indices", "invalid_indices",
             {1u, 0u, 0u, 1u, 0u, 0u, false},
             Live2DRenderFeatureIssue::InvalidIndices,
             "Live2D render feature invalid: drawable index data contains an invalid index"},
            {"blend_mode", "unknown_blend_mode",
             {1u, 0u, 0u, 0u, 1u, 0u, false},
             Live2DRenderFeatureIssue::UnknownBlendMode,
             "Live2D render feature unsupported: drawable uses an unknown blend mode"},
            {"mask_capacity", "excess_mask_contexts",
             {1u, 0u, 0u, 0u, 0u,
                               kLive2DMaxActiveMaskContexts + 1u, false},
             Live2DRenderFeatureIssue::ExcessMaskContexts,
             "Live2D render feature unsupported: active mask context count exceeds V1 capacity"},
        }};

        for (const FeatureCase &test_case : cases)
        {
            SCOPED_TRACE(test_case.name);
            const Live2DRenderFeatureValidation validation =
                ValidateLive2DRenderFeatureReport(test_case.report);
            ASSERT_FALSE(validation.supported);
            ASSERT_EQ(1u, validation.issues.size());
            EXPECT_EQ(test_case.issue, validation.issues.front());
            EXPECT_STREQ(test_case.issue_name,
                         Live2DRenderFeatureIssueName(test_case.issue));
            EXPECT_STREQ(test_case.diagnostic, validation.diagnostic.c_str());
        }
    }

    TEST(Live2DRenderContractTest, ReportsMultipleIssuesInDeterministicOrder)
    {
        const Live2DRenderFeatureReport report{
            1u, 1u, 1u, 1u, 1u, kLive2DMaxActiveMaskContexts + 1u, true};
        const Live2DRenderFeatureValidation validation =
            ValidateLive2DRenderFeatureReport(report);

        ASSERT_EQ(6u, validation.issues.size());
        EXPECT_EQ(Live2DRenderFeatureIssue::OffscreenObjects,
                  validation.issues[0]);
        EXPECT_EQ(Live2DRenderFeatureIssue::BlendGroups, validation.issues[1]);
        EXPECT_EQ(Live2DRenderFeatureIssue::TopologyChange,
                  validation.issues[2]);
        EXPECT_EQ(Live2DRenderFeatureIssue::InvalidIndices,
                  validation.issues[3]);
        EXPECT_EQ(Live2DRenderFeatureIssue::UnknownBlendMode,
                  validation.issues[4]);
        EXPECT_EQ(Live2DRenderFeatureIssue::ExcessMaskContexts,
                  validation.issues[5]);
    }
}
