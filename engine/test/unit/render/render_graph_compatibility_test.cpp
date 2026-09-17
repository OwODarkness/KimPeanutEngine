#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "render/render_graph/render_graph.h"
#include "render/render_pass_declaration.h"

namespace
{
    using kpengine::render::CompiledRenderGraph;
    using kpengine::render::CompileRenderFrameGraph;
    using kpengine::render::FixedRenderPassEntry;
    using kpengine::render::FixedRenderPassId;
    using kpengine::render::GetRenderFramePassEntries;
    using kpengine::render::GraphTextureHandle;
    using kpengine::render::RenderFrameConditions;
    using kpengine::render::RenderGraphAccess;
    using kpengine::render::RenderGraphPassOwner;
    using kpengine::render::RenderPassAccess;
    using kpengine::render::RenderPassCondition;
    using kpengine::render::RenderPassResource;
    using kpengine::render::RenderPassResourceUse;

    constexpr std::size_t kResourceCount =
        static_cast<std::size_t>(RenderPassResource::Count);

    // The passes the conditions schedule: everything except a pass whose
    // condition is not met. The compiled plan culls exactly those.
    std::vector<std::string> ExpectedPlannedPasses(RenderFrameConditions conditions)
    {
        std::vector<std::string> names;
        for (const FixedRenderPassEntry &entry : GetRenderFramePassEntries())
        {
            const bool skipped =
                entry.condition == RenderPassCondition::DiagnosticCaptureRequested &&
                !conditions.diagnostic_capture;
            if (!skipped)
            {
                names.push_back(entry.name);
            }
        }
        return names;
    }
}

TEST(RenderGraphCompatibilityTest, AuthoredDeclarationIsCanonicalAndWellFormed)
{
    const std::vector<FixedRenderPassEntry> &entries = GetRenderFramePassEntries();
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(FixedRenderPassId::Count));
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        // The compiled plan's key is the id, and pass identity indexes the
        // profile arrays and the backend GPU-profile slots.
        EXPECT_EQ(static_cast<std::size_t>(entries[index].id), index);
        EXPECT_FALSE(entries[index].name.empty());
    }
    EXPECT_EQ(entries.back().id, FixedRenderPassId::EditorComposite);
    EXPECT_TRUE(entries.back().terminal);
}

TEST(RenderGraphCompatibilityTest, AuthoredDeclarationCompilesAsAnSsaChainForBothConditionSets)
{
    for (const bool capture_requested : {false, true})
    {
        const RenderFrameConditions conditions{capture_requested};
        const auto result = CompileRenderFrameGraph(conditions);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<std::string> expected = ExpectedPlannedPasses(conditions);
        const std::vector<FixedRenderPassEntry> &entries = GetRenderFramePassEntries();
        ASSERT_EQ(result.graph->Passes().size(), expected.size());

        std::array<uint32_t, kResourceCount> current_versions{};
        for (std::size_t pass_index = 0; pass_index < result.graph->Passes().size(); ++pass_index)
        {
            const CompiledRenderGraph::Pass &pass = result.graph->Passes()[pass_index];
            EXPECT_EQ(pass.name, expected[pass_index]);
            ASSERT_TRUE(pass.user_key.has_value());
            const std::size_t entry_index = static_cast<std::size_t>(*pass.user_key);
            ASSERT_LT(entry_index, entries.size());
            const FixedRenderPassEntry &entry = entries[entry_index];
            EXPECT_EQ(pass.name, entry.name);
            ASSERT_EQ(pass.uses.size(), entry.resources.size());
            for (std::size_t use_index = 0; use_index < pass.uses.size(); ++use_index)
            {
                const auto *texture = std::get_if<GraphTextureHandle>(&pass.uses[use_index].handle);
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

        // The Editor terminal is compiled whatever the conditions, because
        // whether it runs is not known when the frame declares.
        ASSERT_FALSE(result.graph->Passes().empty());
        const CompiledRenderGraph::Pass &terminal = result.graph->Passes().back();
        EXPECT_EQ(terminal.name, "EditorCompositePass");
        EXPECT_EQ(terminal.owner, RenderGraphPassOwner::External);
        EXPECT_TRUE(terminal.terminal);

        // Only the capture variant plans the conversion pass.
        const auto capture_pass = std::find_if(
            result.graph->Passes().begin(), result.graph->Passes().end(),
            [](const CompiledRenderGraph::Pass &pass) {
                return pass.user_key ==
                       static_cast<uint64_t>(FixedRenderPassId::CaptureView);
            });
        EXPECT_EQ(capture_pass != result.graph->Passes().end(), capture_requested);
    }
}
