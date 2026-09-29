#include "shadow_pass.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "asset/mesh.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "render/camera_utils.h"
#include "render/material/material_system.h"
#include "render/passes/shadow_pass_constants.h"
#include "render/passes/shadow_pass_utils.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/render_resource_resolver.h"
#include "render/render_world/render_world.h"

namespace kpengine::render
{
    using shadow_pass_utils::BuildEffectiveDirectionalShadowFit;
    using shadow_pass_utils::ComputeDirectionalShadowStamp;
    using shadow_pass_utils::ComputePointShadowStamp;
    using shadow_pass_utils::DirectionalShadowFit;
    using shadow_pass_utils::IsBoundsInsideSphere;
    using shadow_pass_utils::IsSpotBoundsInsideFrustum;

    bool ShadowPass::PrepareSamplers(graphics::RenderBackend &backend)
    {
        return EnsureSamplers(backend);
    }

    bool ShadowPass::PrepareDirectionalPipeline(
        graphics::RenderBackend &backend, const PreparedRenderAssetCatalog &assets)
    {
        return EnsureShadowPipeline(backend, assets);
    }

    bool ShadowPass::EnsureShadowPipeline(graphics::RenderBackend &backend,
                                          const PreparedRenderAssetCatalog &assets)
    {
        if (pipeline_.IsValid())
        {
            return true;
        }
        const auto program = assets.Get<asset::ShaderProgramResource>(
            assets.GetBuiltIn(BuiltInRenderAsset::DirectionalShadowProgram));
        if (!program)
        {
            return false;
        }
        const auto vert_shader = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto frag_shader = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL));
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status != asset::ShaderStatus::Ready ||
            frag_shader->status != asset::ShaderStatus::Ready)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {{0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                             offsetof(data::Vertex, position)}};
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
        desc.descriptor_binding_descs = {
            {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC,
              ShaderStage::SHADER_STAGE_VERTEX},
             {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC,
              ShaderStage::SHADER_STAGE_VERTEX}},
        };
        pipeline_ = backend.CreatePipelineResource(desc);
        return pipeline_.IsValid();
    }

    bool ShadowPass::EnsureSamplers(graphics::RenderBackend &backend)
    {
        graphics::SamplerSettings settings{};
        settings.address_mode_u = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        settings.address_mode_v = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        settings.address_mode_w = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        settings.enable_anisotropy = false;
        if (!directional_sampler_.IsValid())
            directional_sampler_ = backend.CreateSampler(settings);
        if (!spot_sampler_.IsValid())
            spot_sampler_ = backend.CreateSampler(settings);
        if (!point_sampler_.IsValid())
            point_sampler_ = backend.CreateSampler(settings);
        return directional_sampler_.IsValid() && spot_sampler_.IsValid() &&
               point_sampler_.IsValid();
    }

    void ShadowPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (directional_sampler_.IsValid())
            backend.DestroySampler(directional_sampler_);
        if (spot_sampler_.IsValid())
            backend.DestroySampler(spot_sampler_);
        if (point_sampler_.IsValid())
            backend.DestroySampler(point_sampler_);
        if (pipeline_.IsValid())
            backend.DestroyPipelineResource(pipeline_);
        directional_sampler_ = {};
        spot_sampler_ = {};
        point_sampler_ = {};
        pipeline_ = {};
        directional_valid_ = false;
        point_valid_ = false;
        directional_stamp_ = 0;
        point_stamp_ = 0;
        BeginFrame();
    }

    void ShadowPass::BeginFrame() noexcept
    {
        directional_frame_.reset();
        spot_frame_.reset();
        point_frame_.reset();
        directional_cache_hit_ = false;
        point_cache_hit_ = false;
        spot_recorded_ = false;
        point_recorded_ = false;
    }

    void ShadowPass::Schedule(
        const std::vector<Light> &lights,
        const std::function<bool(ShadowHandle)> &is_shadow_handle_valid,
        SceneDrawRecorder &draw_recorder, RenderResourceResolver &resource_resolver,
        MaterialSystem &materials, const RenderWorld &render_world,
        const Vector3f &camera_position, RenderProfileSnapshot &profile)
    {
        constexpr uint32_t kDirectionalShadowResolution = 2048;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Directional ||
                !light.desc.shadow.has_value() || !is_shadow_handle_valid ||
                !is_shadow_handle_valid(*light.desc.shadow) ||
                !IsShadowKindCompatible(light.desc.type, ShadowKind::Directional2D))
            {
                continue;
            }
            const auto *const directional = std::get_if<DirectionalLightData>(&light.desc.type_data);
            if (!directional || directional->direction.SquareLength() <= 0.0f)
                continue;

            const Vector3f direction = directional->direction.GetSafetyNormalize();
            const std::vector<VisibleMeshSection> &casters =
                draw_recorder.BuildSectionCandidates(resource_resolver);
            DirectionalShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::Directional2D,
                         kDirectionalShadowResolution, 0};
            frame.shadow = *light.desc.shadow;
            frame.light_direction = direction;
            DirectionalShadowFit stamp_fit{};
            const auto effective_fit = BuildEffectiveDirectionalShadowFit(
                casters, camera_position, direction);
            if (effective_fit.has_value())
            {
                ++profile.shadow_fit_evaluations;
                frame.view = effective_fit->matrices.view;
                frame.projection = effective_fit->matrices.projection;
                stamp_fit = *effective_fit;
            }
            else
            {
                constexpr float kHalfExtent = 150.0f;
                constexpr float kDepthRange = 600.0f;
                const Vector3f eye = camera_position - direction * (kDepthRange * 0.5f);
                const Vector3f up = std::abs(direction.y_) > 0.98f
                                        ? Vector3f{0.0f, 0.0f, 1.0f}
                                        : Vector3f{0.0f, 1.0f, 0.0f};
                frame.view = Matrix4f::MakeCameraMatrix(eye, direction, up);
                frame.projection = Matrix4f::MakeOrthProjMatrix(
                    -kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent, 0.1f, kDepthRange);
                stamp_fit.matrices = {frame.view, frame.projection};
            }
            ++profile.shadow_stamp_evaluations;
            frame.validity_stamp = ComputeDirectionalShadowStamp(light, casters, stamp_fit);
            directional_frame_ = frame;
            break;
        }
        directional_cache_hit_ = directional_frame_.has_value() && directional_valid_ &&
                                 directional_frame_->validity_stamp == directional_stamp_;
        profile.shadow_cache_hits = directional_cache_hit_ ? 1 : 0;
        profile.shadow_cache_misses = directional_frame_.has_value() && !directional_cache_hit_ ? 1 : 0;

        constexpr uint32_t kSpotShadowResolution = 1024;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Spot ||
                !light.desc.shadow.has_value() || !is_shadow_handle_valid ||
                !is_shadow_handle_valid(*light.desc.shadow) ||
                !IsShadowKindCompatible(light.desc.type, ShadowKind::Spot2D) ||
                !IsLightDescValid(light.desc))
                continue;
            const auto *const spot = std::get_if<SpotLightData>(&light.desc.type_data);
            if (!spot)
                continue;
            const Vector3f direction = spot->direction.GetSafetyNormalize();
            const float near_plane = std::min(std::max(0.01f, spot->range * 0.001f),
                                              spot->range * 0.5f);
            if (!(near_plane > 0.0f) || !(near_plane < spot->range))
                continue;
            const Vector3f up = std::abs(direction.y_) > 0.98f
                                    ? Vector3f{0.0f, 0.0f, 1.0f}
                                    : Vector3f{0.0f, 1.0f, 0.0f};
            SpotShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::Spot2D, kSpotShadowResolution, 1};
            frame.shadow = *light.desc.shadow;
            frame.position = spot->position;
            frame.light_direction = direction;
            frame.outer_cone_radians = spot->outer_cone_radians;
            frame.near_plane = near_plane;
            frame.far_plane = spot->range;
            frame.view = Matrix4f::MakeCameraMatrix(spot->position, direction, up);
            frame.projection = Matrix4f::MakePerProjMatrix(
                spot->outer_cone_radians * 2.0f, 1.0f, near_plane, spot->range);
            const auto &casters = draw_recorder.BuildSectionCandidates(resource_resolver);
            const bool has_caster = std::any_of(casters.begin(), casters.end(),
                [&](const VisibleMeshSection &candidate) {
                    const auto draw_class = materials.GetDrawClass(candidate.proxy.material);
                    return candidate.proxy.flags.visible && candidate.proxy.flags.casts_shadow &&
                        IsSpotBoundsInsideFrustum(candidate.world_bounds, frame.view,
                                                  frame.outer_cone_radians, frame.near_plane,
                                                  frame.far_plane) && draw_class.has_value() &&
                        *draw_class == MaterialDrawClass::Opaque &&
                        materials.GetInstanceResolution(candidate.proxy.material).state ==
                            MaterialResourceState::Ready;
                });
            if (has_caster)
            {
                spot_frame_ = frame;
                break;
            }
        }

        point_cache_hit_ = false;
        for (const Light &light : lights)
        {
            if (!light.desc.enabled || light.desc.type != LightType::Point ||
                !light.desc.shadow.has_value() || !is_shadow_handle_valid ||
                !is_shadow_handle_valid(*light.desc.shadow) ||
                !IsShadowKindCompatible(light.desc.type, ShadowKind::PointCube) ||
                !IsLightDescValid(light.desc))
                continue;
            const auto *const point = std::get_if<PointLightData>(&light.desc.type_data);
            if (!point)
                continue;
            const float near_plane = std::min(std::max(0.01f, point->range * 0.001f),
                                              point->range * 0.5f);
            if (!(near_plane > 0.0f) || !(near_plane < point->range))
                continue;
            PointShadowFrame frame{};
            frame.job = {light.handle, ShadowKind::PointCube,
                         shadow_pass_detail::kPointShadowFaceResolution, 2};
            frame.shadow = *light.desc.shadow;
            frame.position = point->position;
            frame.near_plane = near_plane;
            frame.far_plane = point->range;
            ++profile.shadow_stamp_evaluations;
            frame.validity_stamp = ComputePointShadowStamp(
                light, render_world.GetRevision(), materials.GetRevision());
            if (point_valid_ && point_stamp_ == frame.validity_stamp)
                point_cache_hit_ = true;
            else
            {
                point_valid_ = false;
                const auto &casters = draw_recorder.BuildSectionCandidates(resource_resolver);
                for (const VisibleMeshSection &candidate : casters)
                {
                    const MeshProxy &proxy = candidate.proxy;
                    const auto draw_class = materials.GetDrawClass(proxy.material);
                    const auto resolution = materials.GetInstanceResolution(proxy.material);
                    if (proxy.flags.visible && proxy.flags.casts_shadow &&
                        IsBoundsInsideSphere(candidate.world_bounds, point->position, point->range) &&
                        draw_class.has_value() && *draw_class == MaterialDrawClass::Opaque &&
                        resolution.state == MaterialResourceState::Ready)
                        frame.caster_candidates.push_back(candidate);
                }
                if (frame.caster_candidates.empty())
                    continue;
            }
            const auto &faces = GetPointShadowFaceTable();
            for (size_t index = 0; index < faces.size(); ++index)
            {
                const PointShadowFaceDesc &face = faces[index];
                const Matrix4f view = Matrix4f::MakeCameraMatrix(
                    point->position, face.direction, face.up);
                const Matrix4f projection = Matrix4f::MakePerProjMatrix(
                    1.570796327f, 1.0f, near_plane, point->range);
                frame.face_view_projections[index] = (projection * view).Transpose();
            }
            point_frame_ = std::move(frame);
            break;
        }
        if (!point_frame_.has_value())
            point_valid_ = false;
        profile.point_shadow_cache_hits = point_cache_hit_ ? 1 : 0;
        profile.point_shadow_cache_misses = point_frame_.has_value() && !point_cache_hit_ ? 1 : 0;
    }

    bool ShadowPass::RecordDirectional(
        graphics::RenderBackend &backend, RenderTarget *target, FrameContext &frame_context,
        RenderResourceResolver &resource_resolver, MaterialSystem &materials,
        SceneDrawRecorder &draw_recorder, ShadowPassDrawCounts &counts)
    {
        if (!target)
            return false;
        auto *const recorder = backend.GetCommandRecorder();
        if (!recorder)
            return false;
        if (directional_cache_hit_)
            return true;
        if (!directional_frame_.has_value())
        {
            directional_valid_ = false;
            return true;
        }
        if (!pipeline_.IsValid())
            return false;
        const auto &shadow = *directional_frame_;
        graphics::PerPassData data{};
        data.camera_data.view = shadow.view.Transpose();
        data.camera_data.proj = shadow.projection.Transpose();
        const UniformAllocation per_pass = frame_context.UpdateStableUniform(
            shadow_pass_detail::kDirectionalPerPassUniformKey, data);
        if (!per_pass.IsValid())
            return false;
        for (const VisibleMeshSection &candidate :
             draw_recorder.BuildSectionCandidates(resource_resolver))
        {
            const MeshProxy &proxy = candidate.proxy;
            const auto draw_class = materials.GetDrawClass(proxy.material);
            if (proxy.flags.casts_shadow && draw_class.has_value() &&
                *draw_class == MaterialDrawClass::Opaque &&
                materials.GetInstanceResolution(proxy.material).state == MaterialResourceState::Ready)
            {
                const SceneDrawRecordResult draw = draw_recorder.RecordShadowCaster(
                    proxy, per_pass, pipeline_, frame_context, resource_resolver,
                    *recorder, candidate.section_index);
                counts.draw_calls += draw.draw_calls;
                counts.sections += draw.sections;
            }
        }
        directional_valid_ = true;
        directional_stamp_ = shadow.validity_stamp;
        return true;
    }

    bool ShadowPass::RecordSpot(
        graphics::RenderBackend &backend, RenderTarget *target, FrameContext &frame_context,
        RenderResourceResolver &resource_resolver, MaterialSystem &materials,
        SceneDrawRecorder &draw_recorder, ShadowPassDrawCounts &counts)
    {
        if (!target)
            return false;
        auto *const recorder = backend.GetCommandRecorder();
        if (!recorder)
            return false;
        if (!spot_frame_.has_value())
            return true;
        if (!pipeline_.IsValid())
            return false;
        const auto &shadow = *spot_frame_;
        graphics::PerPassData data{};
        data.camera_data.view = shadow.view.Transpose();
        data.camera_data.proj = shadow.projection.Transpose();
        const UniformAllocation per_pass = frame_context.UpdateStableUniform(
            shadow_pass_detail::kSpotPerPassUniformKey, data);
        if (!per_pass.IsValid())
            return false;
        for (const VisibleMeshSection &candidate :
             draw_recorder.BuildSectionCandidates(resource_resolver))
        {
            const MeshProxy &proxy = candidate.proxy;
            const auto draw_class = materials.GetDrawClass(proxy.material);
            if (!proxy.flags.visible || !proxy.flags.casts_shadow ||
                !IsSpotBoundsInsideFrustum(candidate.world_bounds, shadow.view,
                                           shadow.outer_cone_radians, shadow.near_plane,
                                           shadow.far_plane) || !draw_class.has_value() ||
                *draw_class != MaterialDrawClass::Opaque ||
                materials.GetInstanceResolution(proxy.material).state != MaterialResourceState::Ready)
                continue;
            const SceneDrawRecordResult draw = draw_recorder.RecordShadowCaster(
                proxy, per_pass, pipeline_, frame_context, resource_resolver,
                *recorder, candidate.section_index);
            counts.draw_calls += draw.draw_calls;
            counts.sections += draw.sections;
        }
        spot_recorded_ = true;
        return true;
    }

    bool ShadowPass::RecordPoint(
        graphics::RenderBackend &backend, RenderTarget *target, FrameContext &frame_context,
        RenderResourceResolver &resource_resolver, SceneDrawRecorder &draw_recorder,
        ShadowPassDrawCounts &counts)
    {
        if (!target)
            return false;
        auto *const recorder = backend.GetCommandRecorder();
        if (!recorder)
            return false;
        if (!point_frame_.has_value())
            return true;
        if (!pipeline_.IsValid())
            return false;
        const auto &shadow = *point_frame_;
        const auto &faces = GetPointShadowFaceTable();
        for (size_t index = 0; index < faces.size(); ++index)
        {
            const PointShadowFaceDesc &face = faces[index];
            const Matrix4f view = Matrix4f::MakeCameraMatrix(shadow.position, face.direction, face.up);
            const Matrix4f projection = Matrix4f::MakePerProjMatrix(
                1.570796327f, 1.0f, shadow.near_plane, shadow.far_plane);
            recorder->SetViewport(graphics::Viewport{
                static_cast<float>(face.tile_x * shadow_pass_detail::kPointShadowFaceResolution),
                static_cast<float>(face.tile_y * shadow_pass_detail::kPointShadowFaceResolution),
                static_cast<float>(shadow_pass_detail::kPointShadowFaceResolution),
                static_cast<float>(shadow_pass_detail::kPointShadowFaceResolution), 0.0f, 1.0f});
            graphics::PerPassData data{};
            data.camera_data.view = view.Transpose();
            data.camera_data.proj = projection.Transpose();
            const UniformAllocation per_pass = frame_context.UpdateStableUniform(
                shadow_pass_detail::kPointPerPassUniformKey + index, data);
            if (!per_pass.IsValid())
                return false;
            for (const VisibleMeshSection &candidate : shadow.caster_candidates)
            {
                if (!camera::IsAABBInsidePerspectiveFace(
                        candidate.world_bounds, view, shadow.near_plane, shadow.far_plane))
                    continue;
                const auto draw = draw_recorder.RecordShadowCaster(
                    candidate.proxy, per_pass, pipeline_, frame_context,
                    resource_resolver, *recorder, candidate.section_index);
                counts.draw_calls += draw.draw_calls;
                counts.sections += draw.sections;
            }
        }
        point_recorded_ = true;
        point_valid_ = true;
        point_stamp_ = shadow.validity_stamp;
        return true;
    }
}
