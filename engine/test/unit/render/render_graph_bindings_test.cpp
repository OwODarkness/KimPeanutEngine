#include <gtest/gtest.h>

#include <array>
#include <string>

#include "render/renderer_frame_targets.h"
#include "render/render_graph/render_graph_bindings.h"
#include "../../support/fake_render_backend.h"

namespace
{
    using namespace kpengine;
    using namespace kpengine::render;

    CompiledRenderFramePlan MakePlan(GraphTextureHandle texture,
                                     GraphBufferHandle geometry,
                                     GraphAccelerationStructureHandle blas)
    {
        CompiledRenderFramePlan plan;
        plan.resources = {
            {RenderFrameResourceRole::SceneColor, texture},
            {RenderFrameResourceRole::SceneGeometry, geometry},
            {RenderFrameResourceRole::SceneBlas, blas}};
        return plan;
    }
}

TEST(RenderGraphBindingsTest, ValidatesExactTypedResourcesAndDeduplicatesGroups)
{
    const GraphTextureHandle texture{1, 2, 0};
    const GraphBufferHandle geometry{1, 3, 1};
    const GraphAccelerationStructureHandle blas{1, 4, 0};
    const CompiledRenderFramePlan plan = MakePlan(texture, geometry, blas);
    auto *const target = reinterpret_cast<RenderTarget *>(uintptr_t{1});
    const std::array buffers{graphics::BufferHandle{10, 1}, graphics::BufferHandle{11, 1},
                             graphics::BufferHandle{10, 1}};
    const std::array structures{graphics::AccelerationStructureHandle{12, 1},
                                graphics::AccelerationStructureHandle{13, 1}};
    RenderGraphBindings bindings;
    std::string error;

    ASSERT_TRUE(bindings.AddTexture(texture, RenderFrameResourceRole::SceneColor, target, error));
    ASSERT_TRUE(bindings.AddBuffers(geometry, RenderFrameResourceRole::SceneGeometry, buffers, error));
    ASSERT_TRUE(bindings.AddAccelerationStructures(blas, RenderFrameResourceRole::SceneBlas,
                                                   structures, error));
    ASSERT_TRUE(bindings.Validate(plan, error)) << error;
    EXPECT_EQ(bindings.ResolveTexture(texture), target);
    EXPECT_EQ(bindings.ResolveBuffers(geometry).size(), 2u);
    EXPECT_EQ(bindings.ResolveAccelerationStructures(blas).size(), 2u);
}

TEST(RenderGraphBindingsTest, RejectsMissingForeignDuplicateAndEmptyBindings)
{
    const GraphTextureHandle texture{1, 2, 0};
    const GraphBufferHandle geometry{1, 3, 0};
    const GraphAccelerationStructureHandle blas{1, 4, 0};
    const CompiledRenderFramePlan plan = MakePlan(texture, geometry, blas);
    auto *const target = reinterpret_cast<RenderTarget *>(uintptr_t{1});
    std::string error;

    RenderGraphBindings missing;
    ASSERT_TRUE(missing.AddTexture(texture, RenderFrameResourceRole::SceneColor, target, error));
    EXPECT_FALSE(missing.Validate(plan, error));

    RenderGraphBindings empty;
    EXPECT_FALSE(empty.AddBuffers(geometry, RenderFrameResourceRole::SceneGeometry,
                                  std::span<const graphics::BufferHandle>{}, error));

    RenderGraphBindings duplicate;
    ASSERT_TRUE(duplicate.AddTexture(texture, RenderFrameResourceRole::SceneColor, target, error));
    EXPECT_FALSE(duplicate.AddTexture(texture, RenderFrameResourceRole::SceneColor, target, error));

    RenderGraphBindings foreign;
    const GraphTextureHandle other_graph{2, 2, 0};
    EXPECT_TRUE(foreign.AddTexture(other_graph, RenderFrameResourceRole::SceneColor, target, error));
    EXPECT_FALSE(foreign.Validate(plan, error));
}

TEST(RenderGraphBindingsTest, TransitionsEveryMemberOfBufferAndAccelerationGroups)
{
    using namespace kpengine;
    using namespace kpengine::render;

    RenderGraphBuilder builder;
    const GraphBufferHandle geometry = builder.ImportBuffer("Geometry");
    const GraphAccelerationStructureHandle blas =
        builder.ImportAccelerationStructure("BLAS");
    auto pass = builder.AddPass({"Build", RenderGraphPassCondition::Always, true, true});
    pass.Read(geometry, RenderGraphUsage::AccelerationStructureBuildInput);
    pass.Read(blas, RenderGraphUsage::AccelerationStructureRead);
    const auto compilation = builder.Compile();
    ASSERT_TRUE(compilation.Succeeded());

    const std::array buffers{graphics::BufferHandle{21, 1}, graphics::BufferHandle{22, 1}};
    const std::array structures{graphics::AccelerationStructureHandle{31, 1},
                                graphics::AccelerationStructureHandle{32, 1}};
    RenderGraphBindings bindings;
    std::string error;
    ASSERT_TRUE(bindings.AddBuffers(geometry, RenderFrameResourceRole::SceneGeometry,
                                    buffers, error));
    ASSERT_TRUE(bindings.AddAccelerationStructures(
        blas, RenderFrameResourceRole::SceneBlas, structures, error));
    auto probe = std::make_shared<test::BackendProbe>();
    test::FakeBackend backend(probe);

    ASSERT_TRUE(bindings.ApplyPassTransitions(*compilation.graph,
                                              compilation.graph->Passes().front(),
                                              *backend.GetCommandRecorder(), error)) << error;
    ASSERT_EQ(probe->buffer_usage_requirements.size(), 2u);
    ASSERT_EQ(probe->acceleration_structure_usage_requirements.size(), 2u);
    EXPECT_EQ(probe->buffer_usage_requirements[0].second,
              graphics::ResourceUsage::AccelerationStructureBuildInput);
    EXPECT_EQ(probe->buffer_usage_requirements[1].second,
              graphics::ResourceUsage::AccelerationStructureBuildInput);
    EXPECT_EQ(probe->acceleration_structure_usage_requirements[0].second,
              graphics::ResourceUsage::AccelerationStructureRead);
    EXPECT_EQ(probe->acceleration_structure_usage_requirements[1].second,
              graphics::ResourceUsage::AccelerationStructureRead);
}

TEST(RenderGraphBindingsTest, PreservesAttachmentSubresourceScope)
{
    using namespace kpengine;
    using namespace kpengine::render;

    auto probe = std::make_shared<test::BackendProbe>();
    test::FakeBackend backend(probe);
    RendererFrameTargets targets;
    targets.Initialize(backend, 320, 200);
    RenderTarget *const target = targets.GetTarget(RenderTargetName::GBuffer);
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->IsValid());

    RenderGraphBuilder builder;
    const GraphTextureHandle imported = builder.ImportTexture("GBuffer");
    auto pass = builder.AddPass({"Diagnostics", RenderGraphPassCondition::Always, true, true});
    const RenderGraphAttachmentScope scope = RenderGraphAttachmentScope::Colors(0b1010, true);
    pass.Read(imported, RenderGraphUsage::Sampled, scope);
    const auto compilation = builder.Compile();
    ASSERT_TRUE(compilation.Succeeded());

    RenderGraphBindings bindings;
    std::string error;
    ASSERT_TRUE(bindings.AddTexture(imported, RenderFrameResourceRole::GBuffer,
                                    target, error));
    ASSERT_TRUE(bindings.ApplyPassTransitions(*compilation.graph,
                                              compilation.graph->Passes().front(),
                                              *backend.GetCommandRecorder(), error)) << error;
    ASSERT_EQ(probe->target_usage_requirements.size(), 1u);
    const auto &requirement = probe->target_usage_requirements.front();
    EXPECT_EQ(requirement.target, target->GetHandle());
    EXPECT_EQ(requirement.usage, graphics::ResourceUsage::Sampled);
    EXPECT_FALSE(requirement.scope.all);
    EXPECT_EQ(requirement.scope.color_mask, 0b1010u);
    EXPECT_TRUE(requirement.scope.depth);
    targets.Cleanup();
}
