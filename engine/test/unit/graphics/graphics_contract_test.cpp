#include <gtest/gtest.h>

#include "base/handle.h"
#include "common/bindless_texture.h"
#include "common/graphics_capabilities.h"
#include "common/pipeline_validation.h"
#include "common/render_target_readback.h"
#include "data/shader.h"
#include "render/pipeline_cache_key.h"
#include "vulkan/vulkan_memory_free_range_list.h"

namespace
{
    using kpengine::graphics::DescriptorBindingDesc;
    using kpengine::graphics::DescriptorType;
    using kpengine::graphics::PipelineDesc;
    using kpengine::graphics::ValidatePipelineDesc;
    using kpengine::graphics::VertexAttributionDesc;
    using kpengine::graphics::VertexBindingDesc;

    kpengine::data::ShaderData MakeOpenGlShader(ShaderStage stage)
    {
        kpengine::data::ShaderData shader{};
        shader.stage = stage;
        shader.api = kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL;
        shader.source = "void main() {}";
        return shader;
    }

    PipelineDesc MakeValidOpenGlPipeline(kpengine::data::ShaderData &vertex,
                                         kpengine::data::ShaderData &fragment)
    {
        PipelineDesc desc{};
        desc.vert_shader = &vertex;
        desc.frag_shader = &fragment;
        desc.binding_descs = {VertexBindingDesc{0, 20, false}};
        desc.attri_descs = {VertexAttributionDesc{0, 0,
            kpengine::graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS, 0}};
        desc.descriptor_binding_descs = {{DescriptorBindingDesc{0, 1,
            DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX}}};
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
        return desc;
    }

    struct TestTag {};
}

TEST(HandleSystem, RejectsForgedAndStaleHandles)
{
    kpengine::HandleSystem<kpengine::Handle<TestTag>> handles;
    const auto first = handles.Create();
    EXPECT_TRUE(handles.Destroy(first));
    EXPECT_EQ(handles.Get(first), KPENGINE_NULL_HANDLE);

    const kpengine::Handle<TestTag> forged{9999, 0};
    EXPECT_FALSE(handles.IsHandleValid(forged));
    EXPECT_EQ(handles.Get(forged), KPENGINE_NULL_HANDLE);
    EXPECT_FALSE(handles.Destroy(forged));
}

TEST(GraphicsCapabilities, DefaultsToThePortableBoundResourcePath)
{
    const kpengine::graphics::GraphicsCapabilities capabilities{};
    EXPECT_EQ(capabilities.max_sampled_textures_per_shader_stage, 0u);
    EXPECT_FALSE(capabilities.bindless_textures);
    EXPECT_EQ(capabilities.bindless_texture_table_capacity, 0u);
    EXPECT_FALSE(capabilities.SupportsBindlessTextures());
}

TEST(RenderTargetReadbackContract, ValidatesOwnedRgba8Output)
{
    kpengine::graphics::CapturedImage image{};
    image.width = 2;
    image.height = 1;
    image.rgba8_pixels = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(image.ExpectedByteCount(), 8u);
    EXPECT_TRUE(image.IsValid());

    image.rgba8_pixels.pop_back();
    EXPECT_FALSE(image.IsValid());
}

TEST(RenderTargetReadbackContract, RejectsInvalidTargetsAndTerminalTransitions)
{
    const kpengine::graphics::RenderTargetReadbackRequest invalid_request{};
    EXPECT_FALSE(invalid_request.IsValid());

    using State = kpengine::graphics::RenderTargetReadbackState;
    EXPECT_TRUE(kpengine::graphics::IsRenderTargetReadbackTransitionValid(
        State::Queued, State::Submitted));
    EXPECT_TRUE(kpengine::graphics::IsRenderTargetReadbackTransitionValid(
        State::Submitted, State::Completed));
    EXPECT_FALSE(kpengine::graphics::IsRenderTargetReadbackTransitionValid(
        State::Queued, State::Completed));
    EXPECT_FALSE(kpengine::graphics::IsRenderTargetReadbackTransitionValid(
        State::Completed, State::Cancelled));
}

TEST(BindlessTextureContract, DefinesAStableSampledTextureTableAbi)
{
    using kpengine::graphics::BindlessTextureTableLayout;
    using kpengine::graphics::GraphicsCapabilities;

    EXPECT_EQ(BindlessTextureTableLayout::shader_abi_version, 1u);
    EXPECT_EQ(BindlessTextureTableLayout::descriptor_set, 1u);
    EXPECT_EQ(BindlessTextureTableLayout::descriptor_binding, 0u);
    EXPECT_TRUE(kpengine::graphics::IsBindlessTextureTableCapacityValid(1));
    EXPECT_TRUE(kpengine::graphics::IsBindlessTextureTableCapacityValid(
        BindlessTextureTableLayout::max_capacity));
    EXPECT_FALSE(kpengine::graphics::IsBindlessTextureTableCapacityValid(0));
    EXPECT_FALSE(kpengine::graphics::IsBindlessTextureTableCapacityValid(
        BindlessTextureTableLayout::max_capacity + 1));

    const GraphicsCapabilities incomplete{0, true, 0};
    const GraphicsCapabilities ready{0, true, 64};
    EXPECT_FALSE(incomplete.SupportsBindlessTextures());
    EXPECT_TRUE(ready.SupportsBindlessTextures());
}

TEST(BindlessTextureContract, UsesTheCommonGenerationalHandleRepresentation)
{
    const kpengine::graphics::BindlessTextureHandle invalid{};
    const kpengine::graphics::BindlessTextureHandle slot{7, 3};

    EXPECT_FALSE(invalid.IsValid());
    EXPECT_TRUE(slot.IsValid());
    EXPECT_EQ(slot.id, 7u);
    EXPECT_EQ(slot.generation, 3u);
}

TEST(BindlessTextureSlotAllocator, DefersReleasedSlotReuseUntilSubmissionCompletes)
{
    kpengine::graphics::BindlessTextureSlotAllocator allocator{2};
    const auto first = allocator.Allocate();
    const auto second = allocator.Allocate();
    EXPECT_TRUE(first.IsValid());
    EXPECT_TRUE(second.IsValid());
    EXPECT_FALSE(allocator.Allocate().IsValid());
    EXPECT_EQ(allocator.GetTelemetry().allocation_failures, 1u);

    EXPECT_TRUE(allocator.Release(first, 5));
    EXPECT_FALSE(allocator.IsAllocated(first));
    EXPECT_FALSE(allocator.Release(first, 5));
    EXPECT_EQ(allocator.GetTelemetry().retired_slots, 1u);
    EXPECT_FALSE(allocator.Allocate().IsValid());

    allocator.CollectCompleted(4);
    EXPECT_FALSE(allocator.Allocate().IsValid());
    allocator.CollectCompleted(5);

    const auto reused = allocator.Allocate();
    EXPECT_EQ(reused.id, first.id);
    EXPECT_NE(reused.generation, first.generation);
    EXPECT_TRUE(allocator.IsAllocated(reused));
    EXPECT_EQ(allocator.GetTelemetry().retired_slots, 0u);
}

TEST(BindlessTextureSlotAllocator, RejectsInvalidCapacity)
{
    kpengine::graphics::BindlessTextureSlotAllocator allocator{
        kpengine::graphics::BindlessTextureTableLayout::max_capacity + 1};

    EXPECT_FALSE(allocator.Allocate().IsValid());
    EXPECT_EQ(allocator.GetTelemetry().capacity, 0u);
}

TEST(PipelineValidation, AcceptsCompleteOpenGlDescription)
{
    kpengine::data::ShaderData vertex = MakeOpenGlShader(ShaderStage::SHADER_STAGE_VERTEX);
    kpengine::data::ShaderData fragment = MakeOpenGlShader(ShaderStage::SHADER_STAGE_FRAGMENT);
    const PipelineDesc desc = MakeValidOpenGlPipeline(vertex, fragment);
    EXPECT_TRUE(ValidatePipelineDesc(desc, kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL));
}

TEST(PipelineValidation, RejectsMissingArtifactAndInvalidBindings)
{
    kpengine::data::ShaderData vertex = MakeOpenGlShader(ShaderStage::SHADER_STAGE_VERTEX);
    kpengine::data::ShaderData fragment = MakeOpenGlShader(ShaderStage::SHADER_STAGE_FRAGMENT);
    PipelineDesc desc = MakeValidOpenGlPipeline(vertex, fragment);

    fragment.source.clear();
    EXPECT_FALSE(ValidatePipelineDesc(desc, kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL));

    fragment.source = "void main() {}";
    desc.binding_descs.push_back(VertexBindingDesc{0, 20, false});
    EXPECT_FALSE(ValidatePipelineDesc(desc, kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL));
}

TEST(PipelineCacheKey, DistinguishesProgramApiAndRenderState)
{
    const kpengine::render::PipelineCacheKey baseline{
        42, kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN, 7};
    const kpengine::render::PipelineCacheKey same{
        42, kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN, 7};
    const kpengine::render::PipelineCacheKey different_api{
        42, kpengine::GraphicsAPIType::GRAPHICS_API_OPENGL, 7};
    const kpengine::render::PipelineCacheKey different_state{
        42, kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN, 8};

    EXPECT_EQ(baseline, same);
    EXPECT_FALSE(baseline == different_api);
    EXPECT_FALSE(baseline == different_state);
    EXPECT_EQ(kpengine::render::PipelineCacheKeyHash{}(baseline),
              kpengine::render::PipelineCacheKeyHash{}(same));
}

TEST(VulkanMemoryFreeRangeList, ReusesMergedRangesWithAlignment)
{
    kpengine::graphics::VulkanMemoryFreeRangeList ranges;
    ranges.Reset(1024);
    const auto first = ranges.Allocate(128, 64);
    const auto second = ranges.Allocate(128, 64);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, 0u);
    EXPECT_EQ(*second, 128u);

    ranges.Free(*first, 128);
    ranges.Free(*second, 128);
    ASSERT_EQ(ranges.Ranges().size(), 1u);
    EXPECT_EQ(ranges.Ranges().front().offset, 0u);
    EXPECT_EQ(ranges.Ranges().front().size, 1024u);

    const auto merged = ranges.Allocate(256, 256);
    ASSERT_TRUE(merged.has_value());
    EXPECT_EQ(*merged, 0u);
}
