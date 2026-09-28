#include <gtest/gtest.h>

#include <array>
#include <string>

#include "render/render_graph/render_graph_bindings.h"

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
