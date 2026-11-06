#include "height_fog_pass.h"

#include "asset/shader.h"
#include "asset/shader_program.h"
#include "graphics/backend/common/command_recorder.h"
#include "render/frame_context.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/render_camera.h"
#include "render/render_target.h"
#include "render/temporal_jitter.h"

namespace kpengine::render
{
    namespace
    {
        struct alignas(16) HeightFogGpuData
        {
            Matrix4f inverse_view_projection;
            Vector4f camera_and_orthographic;
            Vector4f forward_and_affect_sky;
            Vector4f density_height_falloff_distance;
            Vector4f color;
        };
        static_assert(sizeof(HeightFogGpuData) == 128);
    }

    bool HeightFogPass::PrepareResources(graphics::RenderBackend &backend,
                                         const PreparedRenderAssetCatalog &assets)
    {
        if (pipeline_.IsValid())
            return true;
        const auto program = assets.Get<asset::ShaderProgramResource>(
            assets.GetBuiltIn(BuiltInRenderAsset::HeightFogProgram));
        if (!program)
            return false;
        const auto vertex = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto fragment = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vertex || !fragment || !vertex->data || !fragment->data ||
            vertex->status != asset::ShaderStatus::Ready || fragment->status != asset::ShaderStatus::Ready)
            return false;
        auto desc = FullscreenPassResources::DescribePipeline(*vertex->data, *fragment->data);
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA16F};
        desc.descriptor_binding_descs = {{
            {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_FRAGMENT},
        }};
        pipeline_ = backend.CreatePipelineResource(desc);
        return pipeline_.IsValid();
    }

    void HeightFogPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (pipeline_.IsValid())
            backend.DestroyPipelineResource(pipeline_);
        pipeline_ = {};
    }

    bool HeightFogPass::Record(FrameContext &frame, const RenderCamera &camera, Vector2f jitter,
                               const HeightFogSettings &settings, const RenderTarget &hdr,
                               const RenderTarget &gbuffer, const FullscreenPassResources &fullscreen,
                               graphics::CommandRecorder &recorder) const
    {
        const auto scene = hdr.GetColorAttachmentTexture(0);
        const auto depth = gbuffer.GetSampledDepthTexture();
        if (!pipeline_.IsValid() || !scene.IsValid() || !depth.IsValid() || !fullscreen.IsReady())
            return false;
        const HeightFogGpuData data{
            ApplyTemporalJitter(camera.GetViewProjectionMatrix(), jitter).Inverse().Transpose(),
            {camera.GetPosition(), camera.GetProjectionMode() == CameraProjectionMode::Orthographic ? 1.0f : 0.0f},
            {camera.GetForward(), settings.affect_sky ? 1.0f : 0.0f},
            {settings.density, settings.reference_height, settings.height_falloff, settings.max_distance},
            {settings.color[0], settings.color[1], settings.color[2], 0.0f},
        };
        const auto uniform = frame.AllocateUniform(data);
        if (!uniform.IsValid())
            return false;
        const auto sampler = fullscreen.LinearSampler();
        const auto bindings = frame.AllocateResourceBindingSet(pipeline_, {0, {
            graphics::SampledTextureBinding{0, 0, scene, sampler},
            graphics::SampledTextureBinding{0, 1, depth, sampler},
            graphics::UniformBufferBinding{0, 2, uniform.buffer, uniform.offset, uniform.range},
        }});
        if (!bindings.IsValid())
            return false;
        recorder.BindPipeline(pipeline_);
        recorder.BindMesh(fullscreen.Mesh());
        recorder.BindResourceBindings(pipeline_, bindings);
        recorder.DrawIndexed();
        return true;
    }
}
