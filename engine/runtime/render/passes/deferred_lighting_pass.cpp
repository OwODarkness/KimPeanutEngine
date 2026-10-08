#include "deferred_lighting_pass.h"

#include "asset/mesh.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "log/logger.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/passes/gbuffer_pass_constants.h"
#include "render/render_resource_resolver.h"
#include "render/render_shader_data.h"
#include "render/render_world/scene_draw_list.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace kpengine::render
{
    namespace
    {
        std::shared_ptr<const asset::ShaderProgramResource> GetDeferredLightingProgram(
            const PreparedRenderAssetCatalog &assets,
            asset::ShaderProgramVariant variant)
        {
            const auto program = assets.Get<asset::ShaderProgramResource>(
                assets.GetBuiltIn(BuiltInRenderAsset::DeferredLightingProgram));
            if (!program)
                return nullptr;
            for (const ShaderStage stage : {ShaderStage::SHADER_STAGE_VERTEX,
                                            ShaderStage::SHADER_STAGE_FRAGMENT})
            {
                const auto shader = assets.Get<asset::ShaderResource>(program->GetData(
                    stage, ShaderFormat::SHADER_FORMAT_GLSL, variant));
                if (!shader || !shader->data || shader->status != asset::ShaderStatus::Ready)
                    return nullptr;
            }
            return program;
        }

        bool CreateLightingPipeline(graphics::RenderBackend &backend,
                                    const PreparedRenderAssetCatalog &assets,
                                    asset::ShaderProgramVariant variant,
                                    graphics::PipelineHandle &pipeline,
                                    bool ray_query)
        {
            const auto program = GetDeferredLightingProgram(assets, variant);
            if (!program)
                return false;
            const auto vertex = assets.Get<asset::ShaderResource>(program->GetData(
                ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL, variant));
            const auto fragment = assets.Get<asset::ShaderResource>(program->GetData(
                ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL, variant));
            if (!vertex || !fragment || !vertex->data || !fragment->data)
                return false;

            graphics::PipelineDesc desc{};
            desc.vert_shader = vertex->data.get();
            desc.frag_shader = fragment->data.get();
            desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA16F};
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
                {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {6, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {11, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {12, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {13, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {7, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {8, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {9, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {10, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
                {15, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
            }};
            if (ray_query)
            {
                desc.descriptor_binding_descs[0].push_back({
                    14, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE,
                    ShaderStage::SHADER_STAGE_FRAGMENT});
            }
            if (!pipeline.IsValid())
                pipeline = backend.CreatePipelineResource(desc);
            return pipeline.IsValid();
        }
    }

    void DeferredLightingPass::UpdateEnvironment(
        const std::optional<EnvironmentSourceDesc> &source,
        const std::optional<EnvironmentSourceHandle> &source_handle,
        RenderResourceResolver &resolver,
        const PreparedRenderAssetCatalog &prepared_assets)
    {
        environment_bindings_owner_.Update(source, source_handle, resolver,
                                           prepared_assets);
    }

    bool DeferredLightingPass::EnsureEnvironmentFallback(
        RenderResourceResolver &resolver)
    {
        return environment_bindings_owner_.EnsureFallback(resolver);
    }

    bool DeferredLightingPass::PrepareResources(
        graphics::RenderBackend &backend, const PreparedRenderAssetCatalog &assets,
        bool ray_query_shadows_supported)
    {
        ray_query_shadows_supported_ = ray_query_shadows_supported;
        if (!CreateLightingPipeline(backend, assets, asset::ShaderProgramVariant::Bound,
                                    pipeline_, false))
            return false;
        if (!ray_query_shadows_supported_)
            return true;
        return CreateLightingPipeline(backend, assets,
                                      asset::ShaderProgramVariant::RayQuery,
                                      ray_query_pipeline_, true);
    }

    void DeferredLightingPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (pipeline_.IsValid())
            backend.DestroyPipelineResource(pipeline_);
        if (ray_query_pipeline_.IsValid())
            backend.DestroyPipelineResource(ray_query_pipeline_);
        pipeline_ = {};
        ray_query_pipeline_ = {};
        ray_query_shadows_supported_ = false;
        ray_query_shadow_path_active_ = false;
        frame_lighting_binding_ = {};
        environment_bindings_owner_.Clear();
    }

    DeferredLightingRecordResult DeferredLightingPass::RecordGBuffer(
        FrameContext &frame_context, RenderCamera &camera,
        const CameraData &previous_camera, bool camera_history_valid,
        SceneDrawRecorder &draw_recorder, MaterialSystem &materials,
        RenderResourceResolver &resource_resolver,
        graphics::CommandRecorder &recorder, RenderTarget *target)
    {
        DeferredLightingRecordResult result{};
        if (target == nullptr)
            return result;

        const graphics::Extent2D extent = frame_context.GetRenderExtent();
        if (extent.height != 0)
        {
            camera.SetAspect(static_cast<float>(extent.width) /
                             static_cast<float>(extent.height));
            const CameraData camera_data = camera.GetCameraData();
            PerPassData per_pass_data{};
            per_pass_data.camera_data.view = camera_data.view;
            per_pass_data.camera_data.proj = camera_data.proj;
            per_pass_data.previous_view = previous_camera.view;
            per_pass_data.previous_proj = previous_camera.proj;
            per_pass_data.temporal_params = Vector4f{
                static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 0.0f};
            per_pass_data.history_params = Vector4f{
                camera_history_valid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            const std::vector<VisibleMeshSection> visible_sections =
                draw_recorder.BuildVisibleSections(camera.GetViewProjectionMatrix(),
                                                   resource_resolver);
            SceneDrawLists draw_lists = SceneDrawListBuilder::Build(
                visible_sections, materials, resource_resolver, MaterialPass::GBuffer);
            SceneDrawListBuilder::SortOpaqueFrontToBack(
                draw_lists.opaque, camera.GetPosition(), camera.GetForward());
            const UniformAllocation per_pass = frame_context.UpdateStableUniform(
                gbuffer_pass_detail::kPerPassUniformKey, per_pass_data);
            if (!per_pass.IsValid())
                return result;

            for (const SceneDrawItem &item : draw_lists.opaque)
            {
                const SceneDrawRecordResult draw = draw_recorder.RecordMeshProxy(
                    item.proxy, per_pass, frame_context, materials, resource_resolver,
                    recorder, MaterialPass::GBuffer, item.section_index);
                result.draw_calls += draw.draw_calls;
                result.sections += draw.sections;
                if (!draw.succeeded)
                    continue;
                if (item.section_index != std::numeric_limits<uint32_t>::max())
                {
                    const auto *const sections = resource_resolver.FindMeshSections(item.proxy.mesh);
                    if (sections != nullptr && item.section_index < sections->size())
                        result.triangles += (*sections)[item.section_index].index_count / 3U;
                }
                else
                {
                    result.triangles += resource_resolver.GetMeshTriangleCount(item.proxy.mesh);
                }
            }
        }
        result.succeeded = true;
        return result;
    }

    DeferredLightingRecordResult DeferredLightingPass::RecordLighting(
        FrameContext &frame_context, RenderCamera &camera,
        const DeferredLightingFrameInputs &inputs,
        const FullscreenPassResources &fullscreen_resources,
        graphics::CommandRecorder &recorder)
    {
        DeferredLightingRecordResult result{};
        if (!inputs.scene_hdr.IsValid() || !inputs.gbuffer.IsValid() ||
            !inputs.screen_space_ao.IsValid() ||
            !inputs.directional_shadow_target.IsValid() ||
            !inputs.spot_shadow_target.IsValid() || !inputs.point_shadow_target.IsValid())
            return result;
        const bool ray_query_shadows = inputs.ray_query_shadows_requested &&
            ray_query_shadows_supported_ && inputs.scene_tlas.IsValid() &&
            ray_query_pipeline_.IsValid();
        result.ray_query_shadows_active = ray_query_shadows;
        if (ray_query_shadows != ray_query_shadow_path_active_)
        {
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "Deferred lighting shadow path: %s",
                   ray_query_shadows ? "ray_query" : "shadow_map_fallback");
            ray_query_shadow_path_active_ = ray_query_shadows;
        }
        const graphics::PipelineHandle pipeline = ray_query_shadows
            ? ray_query_pipeline_ : pipeline_;
        if (!inputs.frame_lighting.IsValid() || !pipeline.IsValid() ||
            !fullscreen_resources.Mesh().IsValid() ||
            !fullscreen_resources.LinearSampler().IsValid())
            return result;

        DeferredLightingGpuData lighting_data{};
        lighting_data.inverse_view_projection =
            camera.GetViewProjectionMatrix().Inverse().Transpose();
        lighting_data.camera_world_position = Vector4f{camera.GetPosition(), 1.0f};
        lighting_data.environment_ibl_params = Vector4f{
            inputs.environment.ibl_enabled ? 1.0f : 0.0f,
            static_cast<float>(inputs.environment.prefilter_level_count),
            inputs.environment.ibl_intensity,
            inputs.screen_space_ao_enabled ? 1.0f : 0.0f};
        if (inputs.directional_shadow != nullptr)
        {
            const auto &shadow = *inputs.directional_shadow;
            lighting_data.directional_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            lighting_data.directional_shadow_params = Vector4f{
                0.0005f, 0.002f, 1.0f / static_cast<float>(shadow.job.resolution), 0.0f};
        }
        if (inputs.spot_shadow != nullptr)
        {
            const auto &shadow = *inputs.spot_shadow;
            lighting_data.spot_shadow_view_projection =
                (shadow.projection * shadow.view).Transpose();
            lighting_data.spot_shadow_params = Vector4f{
                0.00075f, 0.003f,
                1.0f / static_cast<float>(shadow.job.resolution), 1.0f};
        }
        PointShadowGpuData point_shadow_data{};
        if (inputs.point_shadow != nullptr && inputs.point_shadow_recorded)
        {
            point_shadow_data.face_view_projections =
                inputs.point_shadow->face_view_projections;
            point_shadow_data.atlas_params = Vector4f{
                1.0f / 1536.0f, 1.0f / 1024.0f, 0.00075f, 0.003f};
        }
        const UniformAllocation lighting_constants = frame_context.AllocateUniform(lighting_data);
        const UniformAllocation point_shadow_constants =
            frame_context.AllocateUniform(point_shadow_data);
        if (!lighting_constants.IsValid() || !point_shadow_constants.IsValid())
            return result;

        std::vector<graphics::ResourceBinding> resource_bindings{
            graphics::SampledTextureBinding{0, 0,
                inputs.gbuffer.GetColorAttachmentTexture(0),
                fullscreen_resources.LinearSampler()},
            graphics::SampledTextureBinding{0, 1,
                inputs.gbuffer.GetColorAttachmentTexture(1),
                fullscreen_resources.LinearSampler()},
            graphics::SampledTextureBinding{0, 2,
                inputs.gbuffer.GetColorAttachmentTexture(2),
                fullscreen_resources.LinearSampler()},
            graphics::SampledTextureBinding{0, 3,
                inputs.gbuffer.GetSampledDepthTexture(), fullscreen_resources.LinearSampler()},
            graphics::SampledTextureBinding{0, 15,
                inputs.screen_space_ao,
                fullscreen_resources.LinearSampler()},
            inputs.frame_lighting.GetResourceBinding(),
            graphics::UniformBufferBinding{0, 5, lighting_constants.buffer,
                lighting_constants.offset, lighting_constants.range},
            graphics::UniformBufferBinding{0, 13, point_shadow_constants.buffer,
                point_shadow_constants.offset, point_shadow_constants.range},
            graphics::SampledTextureBinding{0, 6,
                inputs.directional_shadow_target.GetSampledDepthTexture(),
                inputs.directional_shadow_sampler},
            graphics::SampledTextureBinding{0, 11,
                inputs.spot_shadow_target.GetSampledDepthTexture(),
                inputs.spot_shadow_sampler},
            graphics::SampledTextureBinding{0, 12,
                inputs.point_shadow_target.GetSampledDepthTexture(),
                inputs.point_shadow_sampler},
            graphics::SampledTextureBinding{0, 7, inputs.environment.panorama.texture,
                inputs.environment.panorama.sampler},
            graphics::SampledTextureBinding{0, 8, inputs.environment.irradiance.texture,
                inputs.environment.irradiance.sampler},
            graphics::SampledTextureBinding{0, 9,
                inputs.environment.prefiltered_radiance.texture,
                inputs.environment.prefiltered_radiance.sampler},
            graphics::SampledTextureBinding{0, 10, inputs.environment.brdf_lut.texture,
                inputs.environment.brdf_lut.sampler},
        };
        if (ray_query_shadows)
            resource_bindings.emplace_back(graphics::AccelerationStructureBinding{
                0, 14, inputs.scene_tlas});
        const graphics::DescriptorSetHandle bindings =
            frame_context.AllocateResourceBindingSet(
                pipeline, {0, std::move(resource_bindings)});
        if (!bindings.IsValid())
            return result;
        recorder.BindPipeline(pipeline);
        recorder.BindMesh(fullscreen_resources.Mesh());
        recorder.BindResourceBindings(pipeline, bindings);
        recorder.DrawIndexed();
        result.draw_calls = 1;
        result.sections = 1;
        result.succeeded = true;
        return result;
    }
}
