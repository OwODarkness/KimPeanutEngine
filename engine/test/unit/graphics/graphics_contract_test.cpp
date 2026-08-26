#include <gtest/gtest.h>

#include "base/handle.h"
#include "common/graphics_capabilities.h"
#include "common/pipeline_validation.h"
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
