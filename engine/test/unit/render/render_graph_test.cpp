#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>

#include "render/render_graph/render_graph.h"

namespace
{
    using kpengine::render::GraphTextureHandle;
    using kpengine::render::RenderGraphBuilder;
    using kpengine::render::RenderGraphDiagnosticCode;
    using kpengine::render::RenderGraphPassCondition;
    using kpengine::render::RenderGraphPassDesc;
    using kpengine::render::RenderGraphPassOwner;

    bool HasDiagnostic(const kpengine::render::RenderGraphCompileResult &result,
                       RenderGraphDiagnosticCode code)
    {
        return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                           [code](const auto &diagnostic) { return diagnostic.code == code; });
    }

}

TEST(RenderGraphTest, OrdersDependenciesAndUsesDeclarationOrderForIndependentPasses)
{
    RenderGraphBuilder builder;
    const GraphTextureHandle initial = builder.CreateTexture("SceneColor");
    const auto consumer = builder.AddPass({"Consumer", RenderGraphPassCondition::Always, true, true});
    const auto producer = builder.AddPass({"Producer", RenderGraphPassCondition::Always, true, false});
    const auto produced = builder.WriteTexture(producer, initial);
    ASSERT_TRUE(produced.has_value());
    ASSERT_TRUE(builder.ReadTexture(consumer, *produced));
    ASSERT_TRUE(builder.ExportTexture(*produced, "SceneColorExport"));

    const auto result = builder.Compile();
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.graph->Passes().size(), 2U);
    EXPECT_EQ(result.graph->Passes()[0].name, "Producer");
    EXPECT_EQ(result.graph->Passes()[1].name, "Consumer");

    RenderGraphBuilder independent_builder;
    const auto first = independent_builder.AddPass({"First", RenderGraphPassCondition::Always,
                                                     true, true});
    const auto second = independent_builder.AddPass({"Second", RenderGraphPassCondition::Always,
                                                      true, true});
    ASSERT_TRUE(independent_builder.AddDependency(second, first));
    const auto independent_result = independent_builder.Compile();
    ASSERT_TRUE(independent_result.Succeeded());
    EXPECT_EQ(independent_result.graph->Passes()[0].id, first);
    EXPECT_EQ(independent_result.graph->Passes()[1].id, second);
}

TEST(RenderGraphTest, CullsUnreachablePassesAndTracksVersionLifetime)
{
    RenderGraphBuilder builder;
    const GraphTextureHandle imported = builder.ImportTexture("CameraColor");
    const GraphTextureHandle dead = builder.CreateTexture("DeadTexture");
    const auto dead_pass = builder.AddPass({"Dead", RenderGraphPassCondition::Always, true, false});
    ASSERT_TRUE(builder.WriteTexture(dead_pass, dead).has_value());

    const auto producer = builder.AddPass({"Producer", RenderGraphPassCondition::Always, true, false});
    ASSERT_TRUE(builder.ReadTexture(producer, imported));
    const auto produced = builder.WriteTexture(producer, imported);
    ASSERT_TRUE(produced.has_value());
    const auto consumer = builder.AddPass({"Consumer", RenderGraphPassCondition::Always, true, true});
    ASSERT_TRUE(builder.ReadTexture(consumer, *produced));

    const auto result = builder.Compile();
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.graph->Passes().size(), 2U);
    EXPECT_EQ(result.graph->Passes()[0].name, "Producer");
    EXPECT_EQ(result.graph->Passes()[1].name, "Consumer");
    EXPECT_EQ(result.graph->FindPass(dead_pass), nullptr);

    const auto lifetime = std::find_if(
        result.graph->Lifetimes().begin(), result.graph->Lifetimes().end(),
        [&produced](const auto &interval) {
            return std::holds_alternative<GraphTextureHandle>(interval.handle) &&
                   std::get<GraphTextureHandle>(interval.handle) == *produced;
        });
    ASSERT_NE(lifetime, result.graph->Lifetimes().end());
    EXPECT_EQ(lifetime->first_use, 0U);
    EXPECT_EQ(lifetime->last_use, 1U);
}

TEST(RenderGraphTest, CompilesTypedBufferVersions)
{
    RenderGraphBuilder builder;
    const auto input = builder.ImportBuffer("LightData");
    const auto writer = builder.AddPass({"BufferWriter", RenderGraphPassCondition::Always, true, false});
    ASSERT_TRUE(builder.ReadBuffer(writer, input));
    const auto output = builder.WriteBuffer(writer, input);
    ASSERT_TRUE(output.has_value());
    const auto reader = builder.AddPass({"BufferReader", RenderGraphPassCondition::Always, true, true});
    ASSERT_TRUE(builder.ReadBuffer(reader, *output));

    const auto result = builder.Compile();
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.graph->Passes().size(), 2U);
    EXPECT_EQ(result.graph->Passes()[0].name, "BufferWriter");
    EXPECT_EQ(result.graph->Passes()[1].name, "BufferReader");
    EXPECT_TRUE(std::any_of(result.graph->Lifetimes().begin(), result.graph->Lifetimes().end(),
                            [](const auto &interval) {
                                return std::holds_alternative<kpengine::render::GraphBufferHandle>(
                                    interval.handle);
                            }));
}

TEST(RenderGraphTest, RejectsMissingProducersCyclesAndConditionalDependencies)
{
    RenderGraphBuilder missing_builder;
    const auto missing_texture = missing_builder.CreateTexture("Missing");
    const auto missing_pass = missing_builder.AddPass({"MissingReader",
                                                       RenderGraphPassCondition::Always, true, true});
    ASSERT_TRUE(missing_builder.ReadTexture(missing_pass, missing_texture));
    const auto first_missing_result = missing_builder.Compile();
    const auto second_missing_result = missing_builder.Compile();
    ASSERT_TRUE(HasDiagnostic(first_missing_result, RenderGraphDiagnosticCode::MissingProducer));
    ASSERT_EQ(first_missing_result.diagnostics.size(), second_missing_result.diagnostics.size());
    for (std::size_t index = 0; index < first_missing_result.diagnostics.size(); ++index)
    {
        EXPECT_EQ(first_missing_result.diagnostics[index].code,
                  second_missing_result.diagnostics[index].code);
        EXPECT_EQ(first_missing_result.diagnostics[index].message,
                  second_missing_result.diagnostics[index].message);
    }

    RenderGraphBuilder cycle_builder;
    const auto first = cycle_builder.AddPass({"First", RenderGraphPassCondition::Always, true, true});
    const auto second = cycle_builder.AddPass({"Second", RenderGraphPassCondition::Always, true, true});
    ASSERT_TRUE(cycle_builder.AddDependency(first, second));
    ASSERT_TRUE(cycle_builder.AddDependency(second, first));
    EXPECT_TRUE(HasDiagnostic(cycle_builder.Compile(), RenderGraphDiagnosticCode::Cycle));

    RenderGraphBuilder conditional_builder;
    const auto texture = conditional_builder.CreateTexture("Conditional");
    const auto optional = conditional_builder.AddPass(
        {"Optional", RenderGraphPassCondition::Optional, false, false});
    const auto conditional_output = conditional_builder.WriteTexture(optional, texture);
    ASSERT_TRUE(conditional_output.has_value());
    const auto required = conditional_builder.AddPass(
        {"Required", RenderGraphPassCondition::Always, true, true});
    ASSERT_TRUE(conditional_builder.ReadTexture(required, *conditional_output));
    EXPECT_TRUE(HasDiagnostic(conditional_builder.Compile(),
                              RenderGraphDiagnosticCode::ConditionalDependency));
}

TEST(RenderGraphTest, ChainedDeclarationCompilesToTheSameGraphAsTheExplicitOne)
{
    RenderGraphBuilder explicit_builder;
    const auto explicit_camera = explicit_builder.ImportTexture("CameraColor");
    const auto explicit_color = explicit_builder.CreateTexture("Color");
    const auto explicit_first =
        explicit_builder.AddPass({"First", RenderGraphPassCondition::Always, true, false});
    explicit_builder.ReadTexture(explicit_first, explicit_camera);
    const auto explicit_color_v1 = explicit_builder.WriteTexture(explicit_first, explicit_color);
    ASSERT_TRUE(explicit_color_v1.has_value());
    const auto explicit_second =
        explicit_builder.AddPass({"Second", RenderGraphPassCondition::Always, true, false});
    explicit_builder.ReadTexture(explicit_second, *explicit_color_v1);
    explicit_builder.ExportTexture(*explicit_color_v1, "Color");
    const auto explicit_result = explicit_builder.Compile();
    ASSERT_TRUE(explicit_result.Succeeded());

    // The same declaration expressed as a chain. Reads and writes act on the
    // resource's current version, so no handle is threaded by hand.
    RenderGraphBuilder chained_builder;
    const auto chained_camera = chained_builder.ImportTexture("CameraColor");
    const auto chained_color = chained_builder.CreateTexture("Color");
    chained_builder.AddPass({"First", RenderGraphPassCondition::Always, true, false})
        .Read(chained_camera)
        .Write(chained_color);
    chained_builder.AddPass({"Second", RenderGraphPassCondition::Always, true, false})
        .Read(chained_color);
    chained_builder.ExportTexture(chained_builder.CurrentVersion(chained_color), "Color");
    const auto chained_result = chained_builder.Compile();
    ASSERT_TRUE(chained_result.Succeeded());

    ASSERT_EQ(chained_result.graph->Passes().size(), explicit_result.graph->Passes().size());
    for (std::size_t pass_index = 0; pass_index < chained_result.graph->Passes().size(); ++pass_index)
    {
        const auto &chained_pass = chained_result.graph->Passes()[pass_index];
        const auto &explicit_pass = explicit_result.graph->Passes()[pass_index];
        EXPECT_EQ(chained_pass.name, explicit_pass.name);
        ASSERT_EQ(chained_pass.uses.size(), explicit_pass.uses.size());
        for (std::size_t use_index = 0; use_index < chained_pass.uses.size(); ++use_index)
        {
            const auto *chained_texture =
                std::get_if<GraphTextureHandle>(&chained_pass.uses[use_index].handle);
            const auto *explicit_texture =
                std::get_if<GraphTextureHandle>(&explicit_pass.uses[use_index].handle);
            ASSERT_NE(chained_texture, nullptr);
            ASSERT_NE(explicit_texture, nullptr);
            EXPECT_EQ(chained_texture->resource, explicit_texture->resource);
            EXPECT_EQ(chained_texture->version, explicit_texture->version);
            EXPECT_EQ(chained_pass.uses[use_index].access, explicit_pass.uses[use_index].access);
        }
    }
}

TEST(RenderGraphTest, ChainedWriteExtendsTheVersionLaterPassesRead)
{
    RenderGraphBuilder builder;
    const auto color = builder.CreateTexture("Color");
    builder.AddPass({"Writer", RenderGraphPassCondition::Always, true, true}).Write(color);
    builder.AddPass({"Reader", RenderGraphPassCondition::Always, true, true}).Read(color);
    builder.ExportTexture(builder.CurrentVersion(color), "Color");

    const auto result = builder.Compile();
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.graph->Passes().size(), 2U);
    EXPECT_EQ(result.graph->Passes()[0].name, "Writer");
    EXPECT_EQ(result.graph->Passes()[1].name, "Reader");
    const auto *written =
        std::get_if<GraphTextureHandle>(&result.graph->Passes()[0].uses[0].handle);
    const auto *read = std::get_if<GraphTextureHandle>(&result.graph->Passes()[1].uses[0].handle);
    ASSERT_NE(written, nullptr);
    ASSERT_NE(read, nullptr);
    // The reader sees the version the writer produced, not the created one.
    EXPECT_EQ(written->version, read->version);
    EXPECT_GT(read->version, 0U);
}

TEST(RenderGraphTest, RejectsDuplicateEnabledPassKeysButAllowsDisabledReuse)
{
    RenderGraphBuilder builder;
    builder.AddPass({"First", RenderGraphPassCondition::Always, true, false,
                     RenderGraphPassOwner::Renderer, false, 7U});
    builder.AddPass({"Second", RenderGraphPassCondition::Always, true, false,
                     RenderGraphPassOwner::Renderer, false, 7U});
    const auto result = builder.Compile();
    EXPECT_TRUE(HasDiagnostic(result, RenderGraphDiagnosticCode::DuplicatePassKey));
    EXPECT_FALSE(result.Succeeded());

    // A disabled pass never reaches an executor, so it cannot collide.
    RenderGraphBuilder disabled_builder;
    disabled_builder.AddPass({"Enabled", RenderGraphPassCondition::Always, true, false,
                              RenderGraphPassOwner::Renderer, false, 7U});
    disabled_builder.AddPass({"Disabled", RenderGraphPassCondition::Optional, false, false,
                              RenderGraphPassOwner::Renderer, false, 7U});
    EXPECT_TRUE(disabled_builder.Compile().Succeeded());
}

TEST(RenderGraphTest, RejectsCrossGraphHandlesAndAmbiguousWrites)
{
    RenderGraphBuilder first_builder;
    const auto first_texture = first_builder.CreateTexture("Texture");
    const auto first_pass = first_builder.AddPass({"Writer", RenderGraphPassCondition::Always, true, true});
    const auto first_output = first_builder.WriteTexture(first_pass, first_texture);
    ASSERT_TRUE(first_output.has_value());

    RenderGraphBuilder second_builder;
    const auto second_pass = second_builder.AddPass({"Reader", RenderGraphPassCondition::Always, true, true});
    EXPECT_FALSE(second_builder.ReadTexture(second_pass, *first_output));
    EXPECT_FALSE(second_builder.Compile().Succeeded());

    EXPECT_FALSE(first_builder.WriteTexture(first_pass, *first_output).has_value());
    EXPECT_FALSE(first_builder.Compile().Succeeded());
}
