#include "screen_space_ao_pass.h"

#include "asset/mesh.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "render/frame_context.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/render_camera.h"

#include <cstddef>

namespace kpengine::render
{
    namespace
    {
        struct alignas(16) ScreenSpaceAoConstants
        {
            Matrix4f inverse_view_projection;
            Vector4f radius_bias_strength;
        };

        bool CreateAoPipeline(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets,
                              BuiltInRenderAsset built_in, bool filter,
                              graphics::PipelineHandle &pipeline)
        {
            const auto program = assets.Get<asset::ShaderProgramResource>(
                assets.GetBuiltIn(built_in));
            if (!program)
                return false;
            const auto vertex = assets.Get<asset::ShaderResource>(program->GetData(
                ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
            const auto fragment = assets.Get<asset::ShaderResource>(program->GetData(
                ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
            if (!vertex || !fragment || !vertex->data || !fragment->data ||
                vertex->status != asset::ShaderStatus::Ready ||
                fragment->status != asset::ShaderStatus::Ready)
                return false;

            graphics::PipelineDesc desc{};
            desc.vert_shader = vertex->data.get();
            desc.frag_shader = fragment->data.get();
            desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_R8_UNORM};
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
            desc.binding_descs = {{0, sizeof(data::Vertex), false}};
            desc.attri_descs = {
                {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
                 offsetof(data::Vertex, position)},
                {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
                 offsetof(data::Vertex, tex_coord)},
            };
            desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
            desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
            if (filter)
            {
                desc.descriptor_binding_descs = {{
                    {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                     ShaderStage::SHADER_STAGE_FRAGMENT},
                    {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                     ShaderStage::SHADER_STAGE_FRAGMENT},
                    {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                     ShaderStage::SHADER_STAGE_FRAGMENT},
                }};
            }
            else
            {
                desc.descriptor_binding_descs = {{
                    {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                     ShaderStage::SHADER_STAGE_FRAGMENT},
                    {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                     ShaderStage::SHADER_STAGE_FRAGMENT},
                    {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                     ShaderStage::SHADER_STAGE_FRAGMENT},
                }};
            }
            pipeline = backend.CreatePipelineResource(desc);
            return pipeline.IsValid();
        }
    }

    bool ScreenSpaceAoPass::PrepareResources(
        graphics::RenderBackend &backend, const PreparedRenderAssetCatalog &assets)
    {
        return (estimate_pipeline_.IsValid() ||
                CreateAoPipeline(backend, assets,
                    BuiltInRenderAsset::ScreenSpaceAoEstimateProgram, false,
                    estimate_pipeline_)) &&
               (filter_pipeline_.IsValid() ||
                CreateAoPipeline(backend, assets,
                    BuiltInRenderAsset::ScreenSpaceAoFilterProgram, true,
                    filter_pipeline_));
    }

    void ScreenSpaceAoPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (estimate_pipeline_.IsValid())
            backend.DestroyPipelineResource(estimate_pipeline_);
        if (filter_pipeline_.IsValid())
            backend.DestroyPipelineResource(filter_pipeline_);
        estimate_pipeline_ = {};
        filter_pipeline_ = {};
    }

    bool ScreenSpaceAoPass::RecordEstimate(
        FrameContext &frame_context, RenderCamera &camera, RenderTarget &gbuffer,
        const FullscreenPassResources &fullscreen, graphics::CommandRecorder &recorder)
    {
        if (!estimate_pipeline_.IsValid() || !gbuffer.IsValid() ||
            !fullscreen.Mesh().IsValid() || !fullscreen.LinearSampler().IsValid())
            return false;
        ScreenSpaceAoConstants constants{};
        constants.inverse_view_projection =
            camera.GetViewProjectionMatrix().Inverse().Transpose();
        constants.radius_bias_strength = {
            settings_.radius, settings_.bias,
            settings_.enabled ? settings_.strength : 0.0f,
            static_cast<float>(ScreenSpaceAoSampleCount(settings_.quality))};
        const UniformAllocation allocation = frame_context.AllocateUniform(constants);
        if (!allocation.IsValid())
            return false;
        const graphics::DescriptorSetHandle bindings = frame_context.AllocateResourceBindingSet(
            estimate_pipeline_, {0, {
                graphics::SampledTextureBinding{0, 0, gbuffer.GetSampledDepthTexture(),
                    fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 1, gbuffer.GetColorAttachmentTexture(1),
                    fullscreen.LinearSampler()},
                graphics::UniformBufferBinding{0, 3, allocation.buffer,
                    allocation.offset, allocation.range},
            }});
        if (!bindings.IsValid())
            return false;
        recorder.BindPipeline(estimate_pipeline_);
        recorder.BindMesh(fullscreen.Mesh());
        recorder.BindResourceBindings(estimate_pipeline_, bindings);
        recorder.DrawIndexed();
        return true;
    }

    bool ScreenSpaceAoPass::RecordFilter(
        FrameContext &frame_context, RenderTarget &gbuffer, RenderTarget &raw_ao,
        const FullscreenPassResources &fullscreen, graphics::CommandRecorder &recorder)
    {
        if (!filter_pipeline_.IsValid() || !gbuffer.IsValid() || !raw_ao.IsValid() ||
            !fullscreen.Mesh().IsValid() || !fullscreen.LinearSampler().IsValid())
            return false;
        const graphics::DescriptorSetHandle bindings = frame_context.AllocateResourceBindingSet(
            filter_pipeline_, {0, {
                graphics::SampledTextureBinding{0, 0, raw_ao.GetColorAttachmentTexture(0),
                    fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 1, gbuffer.GetSampledDepthTexture(),
                    fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 2, gbuffer.GetColorAttachmentTexture(1),
                    fullscreen.LinearSampler()},
            }});
        if (!bindings.IsValid())
            return false;
        recorder.BindPipeline(filter_pipeline_);
        recorder.BindMesh(fullscreen.Mesh());
        recorder.BindResourceBindings(filter_pipeline_, bindings);
        recorder.DrawIndexed();
        return true;
    }
}
