#include "capture_view_pass.h"

#include "asset/mesh.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "render/light/light_gpu_data.h"
#include "render/prepared_render_asset_catalog.h"

#include <cstddef>

namespace kpengine::render
{
    namespace
    {
        struct alignas(16) CaptureViewGpuData
        {
            Matrix4f inverse_view_projection;
            Matrix4f view;
            Matrix4f directional_shadow_view_projection;
            Vector4f directional_shadow_params;
            Matrix4f spot_shadow_view_projection;
            Vector4f spot_shadow_params;
            Vector4f light_direction_and_view;
            Vector4f depth_params;
            Vector4f punctual_depth_params;
        };
    }

    bool CaptureViewPass::PrepareResources(graphics::RenderBackend &backend,
                                           const PreparedRenderAssetCatalog &assets)
    {
        if (pipeline_.IsValid())
            return true;
        const auto program = assets.Get<asset::ShaderProgramResource>(
            assets.GetBuiltIn(BuiltInRenderAsset::CaptureViewProgram));
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
            {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {6, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {7, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {8, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {9, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {10, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {11, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
        }};
        pipeline_ = backend.CreatePipelineResource(desc);
        return pipeline_.IsValid();
    }

    void CaptureViewPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (pipeline_.IsValid())
            backend.DestroyPipelineResource(pipeline_);
        pipeline_ = {};
    }

    bool CaptureViewPass::Record(
        FrameContext &frame_context, RenderCamera &camera, CaptureView view,
        const CaptureViewFrameInputs &inputs,
        const FullscreenPassResources &fullscreen_resources,
        graphics::CommandRecorder &recorder)
    {
        if (view == CaptureView::SceneColor || !pipeline_.IsValid() ||
            !inputs.output.IsValid() || !inputs.gbuffer.IsValid() ||
            !inputs.directional_shadow_target.IsValid() ||
            !inputs.spot_shadow_target.IsValid() || !inputs.point_shadow_target.IsValid() ||
            !fullscreen_resources.Mesh().IsValid() || !inputs.linear_sampler.IsValid())
            return false;

        CaptureViewGpuData data{};
        data.inverse_view_projection = camera.GetViewProjectionMatrix().Inverse().Transpose();
        data.view = camera.GetCameraData().view;
        const bool has_shadow = inputs.directional_shadow != nullptr;
        Vector3f surface_to_light{0.0f, 1.0f, 0.0f};
        if (has_shadow)
        {
            const auto &shadow = *inputs.directional_shadow;
            data.directional_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            data.directional_shadow_params = Vector4f{
                0.0005f, 0.002f, 1.0f / static_cast<float>(shadow.job.resolution), 0.0f};
            surface_to_light = -shadow.light_direction;
        }
        const bool has_spot_shadow = inputs.spot_shadow != nullptr &&
                                     inputs.spot_shadow_recorded;
        if (has_spot_shadow)
        {
            const auto &shadow = *inputs.spot_shadow;
            data.spot_shadow_view_projection = (shadow.projection * shadow.view).Transpose();
            data.spot_shadow_params = Vector4f{
                0.00075f, 0.003f,
                1.0f / static_cast<float>(shadow.job.resolution), 1.0f};
            if (!has_shadow)
                surface_to_light = -shadow.light_direction;
        }
        const bool has_point_shadow = inputs.point_shadow != nullptr &&
                                      inputs.point_shadow_recorded;
        PointShadowGpuData point_shadow{};
        if (has_point_shadow)
        {
            point_shadow.face_view_projections = inputs.point_shadow->face_view_projections;
            point_shadow.atlas_params = Vector4f{
                1.0f / 1536.0f, 1.0f / 1024.0f, 0.00075f, 0.003f};
            if (view == CaptureView::PointShadowVisibility)
                surface_to_light = inputs.point_shadow->position;
        }
        data.light_direction_and_view = Vector4f{surface_to_light, static_cast<float>(view)};
        data.depth_params = Vector4f{camera.GetFarPlane(), has_shadow ? 1.0f : 0.0f,
                                     has_spot_shadow ? 1.0f : 0.0f,
                                     has_point_shadow ? 1.0f : 0.0f};
        data.punctual_depth_params = Vector4f{
            has_spot_shadow ? inputs.spot_shadow->near_plane : 0.01f,
            has_spot_shadow ? inputs.spot_shadow->far_plane : 1.0f,
            has_point_shadow ? inputs.point_shadow->near_plane : 0.01f,
            has_point_shadow ? inputs.point_shadow->far_plane : 1.0f};

        const UniformAllocation constants = frame_context.AllocateUniform(data);
        const UniformAllocation point_constants = frame_context.AllocateUniform(point_shadow);
        if (!constants.IsValid() || !point_constants.IsValid())
            return false;
        const graphics::DescriptorSetHandle bindings = frame_context.AllocateResourceBindingSet(
            pipeline_, {0, {
                graphics::SampledTextureBinding{0, 2,
                    inputs.gbuffer.GetColorAttachmentTexture(0), inputs.linear_sampler},
                graphics::SampledTextureBinding{0, 3,
                    inputs.gbuffer.GetColorAttachmentTexture(1), inputs.linear_sampler},
                graphics::SampledTextureBinding{0, 4,
                    inputs.gbuffer.GetColorAttachmentTexture(2), inputs.linear_sampler},
                graphics::SampledTextureBinding{0, 5,
                    inputs.gbuffer.GetSampledDepthTexture(), inputs.linear_sampler},
                graphics::SampledTextureBinding{0, 6,
                    inputs.directional_shadow_target.GetSampledDepthTexture(),
                    inputs.directional_shadow_sampler},
                graphics::UniformBufferBinding{0, 7, constants.buffer,
                    constants.offset, constants.range},
                graphics::SampledTextureBinding{0, 8,
                    inputs.spot_shadow_target.GetSampledDepthTexture(),
                    inputs.spot_shadow_sampler},
                graphics::SampledTextureBinding{0, 9,
                    inputs.point_shadow_target.GetSampledDepthTexture(),
                    inputs.point_shadow_sampler},
                graphics::UniformBufferBinding{0, 10, point_constants.buffer,
                    point_constants.offset, point_constants.range},
                graphics::SampledTextureBinding{0, 11,
                    inputs.gbuffer.GetColorAttachmentTexture(3), inputs.linear_sampler},
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
