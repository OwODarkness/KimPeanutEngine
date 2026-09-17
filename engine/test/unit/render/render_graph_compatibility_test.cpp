#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "render/render_graph/render_graph.h"
#include "render/render_pass.h"

namespace
{
    using kpengine::render::CompiledRenderGraph;
    using kpengine::render::FixedRenderPassEntry;
    using kpengine::render::FixedRenderPassFrame;
    using kpengine::render::FixedRenderPassId;
    using kpengine::render::FixedRenderPassSequence;
    using kpengine::render::GraphTextureHandle;
    using kpengine::render::GraphPassId;
    using kpengine::render::RenderGraphAccess;
    using kpengine::render::RenderGraphBuilder;
    using kpengine::render::RenderGraphCompileResult;
    using kpengine::render::RenderGraphPassCondition;
    using kpengine::render::RenderGraphPassDesc;
    using kpengine::render::RenderGraphPassOwner;
    using kpengine::render::RenderPassAccess;
    using kpengine::render::RenderPassCondition;
    using kpengine::render::RenderPassExecutionOwner;
    using kpengine::render::RenderPassOutcome;
    using kpengine::render::RenderPassResource;
    using kpengine::render::RenderPassResourceUse;

    constexpr std::size_t kResourceCount =
        static_cast<std::size_t>(RenderPassResource::Count);

    std::vector<FixedRenderPassEntry> MakeCanonicalEntries()
    {
        return {
            {FixedRenderPassId::DirectionalShadow, "DirectionalShadowPass",
             {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::SpotShadow, "SpotShadowPass",
             {{RenderPassResource::SpotShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::PointShadow, "PointShadowPass",
             {{RenderPassResource::PointShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::GBuffer, "GBufferPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::DeferredLighting, "DeferredLightingPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
              {RenderPassResource::SpotShadow, RenderPassAccess::Read},
              {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneHdr, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::ToneMap, "ToneMapPass",
             {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::CaptureView, "CaptureViewPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
              {RenderPassResource::SpotShadow, RenderPassAccess::Read},
              {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Read},
              {RenderPassResource::CaptureOutput, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer,
             RenderPassCondition::DiagnosticCaptureRequested, false},
            {FixedRenderPassId::EditorComposite, "EditorCompositePass",
             {{RenderPassResource::SceneColor, RenderPassAccess::Read}},
             RenderPassExecutionOwner::External, RenderPassCondition::ExternalRequest, true},
        };
    }

    std::optional<FixedRenderPassSequence> MakeCanonical(std::string &error)
    {
        return FixedRenderPassSequence::Create(MakeCanonicalEntries(), error);
    }

    RenderGraphCompileResult CompileCanonicalGraph(const FixedRenderPassSequence &sequence,
                                                   bool capture_requested,
                                                   bool external_requested)
    {
        RenderGraphBuilder graph;
        std::array<GraphTextureHandle, kResourceCount> current_versions{};
        const std::array<const char *, kResourceCount> resource_names{
            "SceneColor", "SceneHdr", "GBuffer", "DirectionalShadow", "SpotShadow",
            "PointShadow", "CaptureOutput"};
        for (std::size_t resource_index = 0; resource_index < kResourceCount; ++resource_index)
        {
            current_versions[resource_index] =
                graph.CreateTexture(resource_names[resource_index]);
        }

        for (const FixedRenderPassEntry &entry : sequence.Entries())
        {
            const bool enabled =
                entry.condition == RenderPassCondition::Always ||
                (entry.condition == RenderPassCondition::DiagnosticCaptureRequested &&
                 capture_requested) ||
                (entry.condition == RenderPassCondition::ExternalRequest &&
                 external_requested);
            const RenderGraphPassCondition condition =
                entry.condition == RenderPassCondition::Always
                    ? RenderGraphPassCondition::Always
                    : RenderGraphPassCondition::Optional;
            const RenderGraphPassOwner owner =
                entry.owner == RenderPassExecutionOwner::External
                    ? RenderGraphPassOwner::External
                    : RenderGraphPassOwner::Renderer;
            const GraphPassId pass = graph.AddPass(
                RenderGraphPassDesc{entry.name, condition, enabled,
                                     owner == RenderGraphPassOwner::External, owner,
                                     entry.terminal});
            for (const RenderPassResourceUse &use : entry.resources)
            {
                const std::size_t resource_index = static_cast<std::size_t>(use.resource);
                if (use.access == RenderPassAccess::Read)
                {
                    graph.ReadTexture(pass, current_versions[resource_index]);
                }
                else
                {
                    const auto next_version =
                        graph.WriteTexture(pass, current_versions[resource_index]);
                    if (!next_version.has_value())
                    {
                        return graph.Compile();
                    }
                    current_versions[resource_index] = *next_version;
                }
            }
        }

        graph.ExportTexture(current_versions[static_cast<std::size_t>(RenderPassResource::SceneColor)],
                            "SceneColor");
        if (capture_requested)
        {
            graph.ExportTexture(
                current_versions[static_cast<std::size_t>(RenderPassResource::CaptureOutput)],
                "CaptureOutput");
        }
        return graph.Compile();
    }

    std::vector<std::string> ExpectedExecutedPasses(const FixedRenderPassSequence &sequence,
                                                    bool capture_requested,
                                                    bool external_requested)
    {
        FixedRenderPassFrame frame(sequence, capture_requested);
        std::vector<std::string> names;
        EXPECT_TRUE(frame.ExecuteRenderer([&](FixedRenderPassId id) {
            names.push_back(sequence.Entries()[static_cast<std::size_t>(id)].name);
            return true;
        }));
        if (external_requested)
        {
            EXPECT_TRUE(frame.ExecuteExternal([&] {
                names.push_back(sequence.Entries()[static_cast<std::size_t>(
                    FixedRenderPassId::EditorComposite)].name);
            }));
        }
        std::string error;
        EXPECT_TRUE(frame.Finalize(error)) << error;
        return names;
    }
}

TEST(RenderGraphCompatibilityTest, CanonicalEightPassesCompileAsAnSsaChain)
{
    std::string error;
    const auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;

    for (const auto [capture_requested, external_requested] :
         {std::pair{false, false}, std::pair{false, true}, std::pair{true, true}})
    {
        const auto result =
            CompileCanonicalGraph(*sequence, capture_requested, external_requested);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<std::string> expected =
            ExpectedExecutedPasses(*sequence, capture_requested, external_requested);
        ASSERT_EQ(result.graph->Passes().size(), expected.size());

        std::array<uint32_t, kResourceCount> current_versions{};
        for (std::size_t pass_index = 0; pass_index < result.graph->Passes().size(); ++pass_index)
        {
            const CompiledRenderGraph::Pass &pass = result.graph->Passes()[pass_index];
            EXPECT_EQ(pass.name, expected[pass_index]);
            const FixedRenderPassEntry &entry = sequence->Entries()[pass.id.index];
            ASSERT_EQ(pass.uses.size(), entry.resources.size());
            for (std::size_t use_index = 0; use_index < pass.uses.size(); ++use_index)
            {
                const auto *texture =
                    std::get_if<GraphTextureHandle>(&pass.uses[use_index].handle);
                ASSERT_NE(texture, nullptr);
                const std::size_t resource_index =
                    static_cast<std::size_t>(entry.resources[use_index].resource);
                EXPECT_EQ(texture->resource, resource_index);
                const RenderGraphAccess expected_access =
                    entry.resources[use_index].access == RenderPassAccess::Read
                        ? RenderGraphAccess::Read
                        : RenderGraphAccess::Write;
                EXPECT_EQ(pass.uses[use_index].access, expected_access);
                if (expected_access == RenderGraphAccess::Write)
                {
                    EXPECT_EQ(texture->version, current_versions[resource_index] + 1U);
                    current_versions[resource_index] = texture->version;
                }
                else
                {
                    EXPECT_EQ(texture->version, current_versions[resource_index]);
                }
            }
        }

        if (external_requested)
        {
            ASSERT_FALSE(result.graph->Passes().empty());
            EXPECT_EQ(result.graph->Passes().back().owner, RenderGraphPassOwner::External);
            EXPECT_TRUE(result.graph->Passes().back().terminal);
        }
    }
}
