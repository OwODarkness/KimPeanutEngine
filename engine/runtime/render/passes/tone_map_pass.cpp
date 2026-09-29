#include "tone_map_pass.h"

#include "asset/mesh.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/shader_signature.h"

#include <cstddef>

namespace kpengine::render
{
    namespace
    {
        constexpr float kExposure = 1.0f;
        constexpr uint32_t kToneMapOperatorReinhard = 1;
        constexpr uint32_t kOutputTransferSrgb = 1;
    }

    ToneMapOutputPolicy ToneMapPass::OutputPolicy() const noexcept
    {
        return {kExposure, kToneMapOperatorReinhard, kOutputTransferSrgb};
    }

    bool ToneMapPass::PrepareResources(graphics::RenderBackend &backend,
                                       const PreparedRenderAssetCatalog &assets)
    {
        if (pipeline_.IsValid())
            return true;
        const auto program = assets.Get<asset::ShaderProgramResource>(
            assets.GetBuiltIn(BuiltInRenderAsset::ToneMapProgram));
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
        shader_signature_ = 1469598103934665603ull;
        detail::AddShaderSignature(shader_signature_, *vertex->data);
        detail::AddShaderSignature(shader_signature_, *fragment->data);
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
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
        desc.descriptor_binding_descs = {{
            {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
        }};
        pipeline_ = backend.CreatePipelineResource(desc);
        return pipeline_.IsValid();
    }

    void ToneMapPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (pipeline_.IsValid())
            backend.DestroyPipelineResource(pipeline_);
        pipeline_ = {};
        shader_signature_ = 0;
    }

    bool ToneMapPass::Record(
        FrameContext &frame_context, RenderTarget &hdr_source,
        RenderTarget *gbuffer_source, RenderTarget *path_trace_guide,
        RenderTarget &scene_output, const FullscreenPassResources &fullscreen_resources,
        graphics::CommandRecorder &recorder, bool path_tracing_active,
        const PathTraceSettings &settings, uint32_t sample_count)
    {
        if (!pipeline_.IsValid() || !fullscreen_resources.Mesh().IsValid() ||
            !fullscreen_resources.LinearSampler().IsValid() ||
            (!path_tracing_active && gbuffer_source == nullptr) ||
            (path_tracing_active && path_trace_guide == nullptr) ||
            !scene_output.IsValid() || !hdr_source.IsValid())
            return false;

        const float reconstruction = path_tracing_active
            ? (settings.reconstruction == PathTraceReconstruction::GuidedPreview
                   ? 1.0f
                   : (settings.reconstruction == PathTraceReconstruction::VarianceDenoise
                          ? 2.0f
                          : 0.0f))
            : 0.0f;
        const UniformAllocation options = frame_context.AllocateUniform(Vector4f{
            path_tracing_active ? 0.0f : 1.0f, reconstruction,
            static_cast<float>(sample_count + settings.samples_per_dispatch), 0.0f});
        if (!options.IsValid())
            return false;
        const graphics::TextureHandle auxiliary = path_tracing_active
            ? path_trace_guide->GetColorAttachmentTexture(0)
            : gbuffer_source->GetColorAttachmentTexture(3);
        const graphics::TextureHandle history_or_guide = path_tracing_active
            ? path_trace_guide->GetColorAttachmentTexture(0)
            : hdr_source.GetColorAttachmentTexture(0);
        const graphics::DescriptorSetHandle bindings = frame_context.AllocateResourceBindingSet(
            pipeline_, {0, {
                graphics::SampledTextureBinding{0, 2,
                    hdr_source.GetColorAttachmentTexture(0), fullscreen_resources.LinearSampler()},
                graphics::SampledTextureBinding{0, 3, auxiliary,
                    fullscreen_resources.LinearSampler()},
                graphics::SampledTextureBinding{0, 5, history_or_guide,
                    fullscreen_resources.LinearSampler()},
                graphics::UniformBufferBinding{0, 4, options.buffer,
                    options.offset, options.range},
            }});
        if (!bindings.IsValid())
            return false;
        recorder.BindPipeline(pipeline_);
        recorder.BindMesh(fullscreen_resources.Mesh());
        recorder.BindResourceBindings(pipeline_, bindings);
        recorder.DrawIndexed();
        return true;
    }
}
