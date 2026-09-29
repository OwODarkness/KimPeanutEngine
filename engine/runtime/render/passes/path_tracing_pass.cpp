#include "path_tracing_pass.h"

#include "asset/shader.h"
#include "asset/shader_program.h"
#include "log/logger.h"
#include "render/path_trace_history_progress.h"
#include "render/path_trace_history_signature.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/renderer_frame_targets.h"
#include "render/shader_signature.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace kpengine::render
{
    namespace
    {
        namespace path_trace_data = ray_tracing::path_trace_scene_data;

        bool GetPreparedPathTracingProgram(
            const PreparedRenderAssetCatalog &assets,
            std::shared_ptr<const asset::ShaderProgramResource> &out_program)
        {
            out_program = assets.Get<asset::ShaderProgramResource>(
                assets.GetBuiltIn(BuiltInRenderAsset::RayTracingPathTracerProgram));
            if (!out_program)
                return false;
            for (const ShaderStage stage : {ShaderStage::SHADER_STAGE_RAYGEN,
                                            ShaderStage::SHADER_STAGE_MISS,
                                            ShaderStage::SHADER_STAGE_VISIBILITY_MISS,
                                            ShaderStage::SHADER_STAGE_CLOSEST_HIT})
            {
                const asset::AssetID shader_id = out_program->GetData(
                    stage, ShaderFormat::SHADER_FORMAT_GLSL);
                if (stage == ShaderStage::SHADER_STAGE_VISIBILITY_MISS &&
                    !shader_id.IsValid())
                    continue;
                const auto shader = assets.Get<asset::ShaderResource>(shader_id);
                if (!shader || !shader->data || shader->status != asset::ShaderStatus::Ready ||
                    shader->data->api != GraphicsAPIType::GRAPHICS_API_VULKAN ||
                    shader->data->byte_code.empty())
                {
                    out_program.reset();
                    return false;
                }
            }
            return true;
        }
    }

    bool PathTracingPass::PrepareResources(
        graphics::RenderBackend &backend, const PreparedRenderAssetCatalog &assets)
    {
        if (pipeline_.IsValid())
            return true;
        if (!backend.GetCapabilities().SupportsRayTracingPipeline() ||
            !backend.GetCapabilities().SupportsBindlessTextures())
            return false;
        graphics::RayTracingResourceOwner *const owner =
            backend.GetRayTracingResourceOwner();
        if (!owner)
            return false;

        std::shared_ptr<const asset::ShaderProgramResource> program;
        if (!GetPreparedPathTracingProgram(assets, program))
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Path tracing is unavailable: Vulkan RT shader program is not ready");
            return false;
        }
        const auto raygen = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_RAYGEN, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto miss = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_MISS, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto visibility_miss = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_VISIBILITY_MISS, ShaderFormat::SHADER_FORMAT_GLSL));
        const auto closest_hit = assets.Get<asset::ShaderResource>(program->GetData(
            ShaderStage::SHADER_STAGE_CLOSEST_HIT, ShaderFormat::SHADER_FORMAT_GLSL));
        graphics::RayTracingPipelineDesc desc{};
        desc.ray_generation_shader = raygen->data.get();
        desc.miss_shader = miss->data.get();
        if (visibility_miss && visibility_miss->data &&
            visibility_miss->status == asset::ShaderStatus::Ready)
            desc.visibility_miss_shader = visibility_miss->data.get();
        desc.closest_hit_shader = closest_hit->data.get();
        shader_signature_ = 1469598103934665603ull;
        detail::AddShaderSignature(shader_signature_, *raygen->data);
        detail::AddShaderSignature(shader_signature_, *miss->data);
        if (desc.visibility_miss_shader != nullptr)
            detail::AddShaderSignature(shader_signature_, *desc.visibility_miss_shader);
        detail::AddShaderSignature(shader_signature_, *closest_hit->data);
        desc.max_recursion_depth = 1;
        desc.descriptor_binding_descs = {{
            {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_STORAGE_IMAGE,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_CLOSEST_HIT},
            {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_MISS},
            {35, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_STORAGE_IMAGE,
             ShaderStage::SHADER_STAGE_RAYGEN},
            {36, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_STORAGE_IMAGE,
             ShaderStage::SHADER_STAGE_RAYGEN},
        }};
        desc.descriptor_binding_descs.emplace_back();
        KP_LOG("RenderLog", LOG_LEVEL_INFO, "Creating Vulkan ray-tracing pipeline");
        pipeline_ = owner->CreateRayTracingPipeline(desc);
        if (!pipeline_.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Path tracing is unavailable: Vulkan RT pipeline creation failed");
            return false;
        }
        return true;
    }

    bool PathTracingPass::EnsureHistoryTargets(graphics::RenderBackend &backend,
                                               uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return false;
        if (history_targets_[0] && history_targets_[1] &&
            history_targets_[0]->IsValid() && history_targets_[1]->IsValid() &&
            history_targets_[0]->GetWidth() == width &&
            history_targets_[0]->GetHeight() == height && guide_target_ &&
            guide_target_->IsValid() && guide_target_->GetWidth() == width &&
            guide_target_->GetHeight() == height)
            return true;

        if (history_targets_[0] || history_targets_[1] || guide_target_)
        {
            ReleaseBindings(backend);
            backend.WaitIdle();
        }
        const graphics::RenderTargetDesc desc =
            RendererFrameTargets::DescribeSceneHdr(width, height);
        const auto rollback_targets = [this]() {
            for (auto &target : history_targets_)
            {
                if (target)
                    target->Cleanup();
                target.reset();
            }
            if (guide_target_)
                guide_target_->Cleanup();
            guide_target_.reset();
            sample_count_ = 0;
            write_index_ = 0;
            history_signature_ = 0;
        };
        try
        {
            for (auto &target : history_targets_)
            {
                if (!target)
                    target = std::make_unique<RenderTarget>();
                target->Initialize(backend, desc);
                if (!target->IsValid())
                {
                    rollback_targets();
                    return false;
                }
            }
            if (!guide_target_)
                guide_target_ = std::make_unique<RenderTarget>();
            guide_target_->Initialize(backend, desc);
            if (!guide_target_->IsValid())
            {
                rollback_targets();
                return false;
            }
        }
        catch (...)
        {
            rollback_targets();
            throw;
        }
        sample_count_ = 0;
        write_index_ = 0;
        history_signature_ = 0;
        return true;
    }

    uint64_t PathTracingPass::HistorySignature(
        uint32_t width, uint32_t height,
        const ray_tracing::RayTracingSceneView &scene,
        const EnvironmentFrameBindings &environment, const RenderCamera &camera,
        const PathTraceSettings &settings)
    {
        detail::PathTraceHistorySignatureInput input{};
        input.width = width;
        input.height = height;
        input.scene_signature = scene.instance_signature;
        input.geometry_count = scene.geometries.size();
        input.material_signature = scene.material_signature;
        input.lighting_signature = scene.lighting_signature;
        if (environment.HasCompleteBindings())
        {
            input.lighting_signature ^= environment.source_asset.Pack();
            input.lighting_signature *= 1099511628211ull;
            input.lighting_signature ^= static_cast<uint64_t>(
                std::hash<float>{}(environment.ibl_intensity));
        }
        input.pipeline_id = pipeline_.id;
        input.pipeline_generation = pipeline_.generation;
        input.shader_signature = shader_signature_;
        input.probe_mode = PackPathTraceHistoryMode(settings);
        input.ray_parameters = {path_trace_data::kRayMinimumDistance,
                                path_trace_data::kRayMaximumDistance,
                                path_trace_data::kSecondaryRayOffset, 0.0f};
        input.batch_samples_per_dispatch = settings.samples_per_dispatch;
        input.integrator_parameters = {
            path_trace_data::kDirectLightSamples,
            settings.maximum_continuation_bounces,
            path_trace_data::kIntegratorVersion};
        input.rng_seed = path_trace_data::kRngSeed;
        input.rng_policy_version = path_trace_data::kRngPolicyVersion;
        const Matrix4f view_projection = camera.GetViewProjectionMatrix();
        std::size_t value_index = 0;
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                input.view_projection[value_index++] = view_projection[row][column];
        const Vector3f position = camera.GetPosition();
        for (std::size_t axis = 0; axis < 3; ++axis)
            input.camera_position[axis] = position[axis];
        pending_history_reset_reason_ =
            detail::DescribePathTraceHistoryChange(previous_history_input_, input);
        previous_history_input_ = input;
        return detail::ComputePathTraceHistorySignature(input);
    }

    const char *PathTracingPass::UpdateHistorySignature(uint64_t signature) noexcept
    {
        if (signature == history_signature_)
            return nullptr;
        const char *const reason = history_signature_ == 0
            ? "history_uninitialized" : pending_history_reset_reason_;
        if (sample_count_ >= 16 || history_signature_ == 0)
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "Path trace history reset: reason=%s, previous_samples=%u",
                   reason, sample_count_);
        sample_count_ = 0;
        history_signature_ = signature;
        return reason;
    }

    void PathTracingPass::CommitFrame(bool finalized, bool frame_execution_failed,
                                      bool required_pass_failed,
                                      uint32_t samples_per_dispatch) noexcept
    {
        random_frame_index_ = detail::CommitPathTraceRandomFrameIndex(
            random_frame_index_, finalized, frame_execution_failed,
            required_pass_failed, active_, samples_per_dispatch);
        const detail::PathTraceHistoryProgress progress =
            detail::CommitPathTraceHistoryProgress(
                {sample_count_, write_index_}, finalized, frame_execution_failed,
                required_pass_failed, active_, samples_per_dispatch);
        sample_count_ = progress.sample_count;
        write_index_ = progress.write_index;
    }

    bool PathTracingPass::CanTraceScene(
        const ray_tracing::RayTracingSceneView &scene) const noexcept
    {
        return scene.geometries.size() <= path_trace_data::kMaximumSceneRecords &&
               scene.path_instances.size() <= path_trace_data::kMaximumSceneRecords &&
               scene.path_materials.size() <= path_trace_data::kMaximumSceneRecords &&
               scene.path_lights.size() <= 128;
    }

    bool PathTracingPass::ShouldReportSceneCapacity(uint64_t signature) noexcept
    {
        if (scene_capacity_report_signature_ == signature)
            return false;
        scene_capacity_report_signature_ = signature;
        return true;
    }

    uint32_t PathTracingPass::MaximumSceneRecords() const noexcept
    {
        return static_cast<uint32_t>(path_trace_data::kMaximumSceneRecords);
    }

    bool PathTracingPass::Record(
        graphics::RenderBackend &backend, FrameContext &frame_context,
        RenderCamera &camera, const PathTraceSettings &settings,
        const ray_tracing::RayTracingSceneView &scene,
        const EnvironmentFrameBindings &environment,
        const RenderGraphPassContext &pass_context)
    {
        graphics::CommandRecorder &recorder = pass_context.GetRecorder();
        graphics::RayTracingResourceOwner *const owner =
            backend.GetRayTracingResourceOwner();
        RenderTarget *const hdr_target = pass_context.ResolveTexture(
            RenderFrameResourceRole::SceneHdr, RenderGraphAccess::Write);
        RenderTarget *const history_target =
            pass_context.ResolveTexture(RenderFrameResourceRole::PathTraceHistory);
        const auto scene_geometry = pass_context.ResolveBuffers(
            RenderFrameResourceRole::SceneGeometry, RenderGraphAccess::Read);
        const auto scene_tlas = pass_context.ResolveAccelerationStructures(
            RenderFrameResourceRole::SceneTlas, RenderGraphAccess::Read);
        const graphics::AccelerationStructureHandle top_level = scene_tlas.size() == 1
            ? scene_tlas.front() : graphics::AccelerationStructureHandle{};
        if (!owner || !pipeline_.IsValid() || !hdr_target || !history_target ||
            !top_level.IsValid() || !scene.reference_table.IsValid())
            return false;

        ray_tracing::path_trace_scene_data::PathTracingCameraGpuData camera_data{};
        camera_data.inverse_view_projection =
            camera.GetViewProjectionMatrix().Inverse().Transpose();
        camera_data.camera_position = Vector4f{camera.GetPosition(), 1.0f};
        camera_data.rng_seed = path_trace_data::kRngSeed;
        camera_data.sample_count = sample_count_;
        camera_data.samples_per_dispatch = settings.samples_per_dispatch;
        camera_data.probe_mode = PackPathTraceShaderMode(settings);
        camera_data.random_frame_index = random_frame_index_;
        camera_data.light_center = Vector4f{0.0f, 0.0f, 0.0f,
            static_cast<float>(settings.maximum_continuation_bounces)};
        camera_data.scene_data[0] = static_cast<uint32_t>(scene.geometries.size());
        camera_data.scene_data[1] = static_cast<uint32_t>(scene.path_instances.size());
        camera_data.scene_data[2] = static_cast<uint32_t>(scene.path_materials.size());
        camera_data.scene_data[3] = static_cast<uint32_t>(scene.path_lights.size());
        camera_data.light_radiance = Vector4f{0.0f, 0.0f, 0.0f,
            environment.ibl_enabled ? environment.ibl_intensity : 0.0f};
        if (!CanTraceScene(scene))
            return false;
        const UniformAllocation camera_uniform = frame_context.UpdateStableUniform(
            path_trace_data::kCameraUniformKey, camera_data);
        if (!camera_uniform.IsValid() || scene.geometries.empty() || scene_geometry.empty())
            return false;
        const graphics::RayTracingBufferReferenceTableHandle reference_table =
            scene.reference_table;

        const size_t frame_index = frame_context.GetFrameIndex();
        if (bindings_.size() <= frame_index)
            bindings_.resize(frame_index + 1);
        BindingCache &binding_cache = bindings_[frame_index][write_index_ & 1u];
        const graphics::TextureHandle hdr_output =
            hdr_target->GetColorAttachmentTexture(0);
        const graphics::TextureHandle history_output =
            history_target->GetColorAttachmentTexture(0);
        RenderTarget *const guide_target = pass_context.ResolveTexture(
            RenderFrameResourceRole::PathTraceGuide, RenderGraphAccess::Write);
        if (guide_target == nullptr)
            return false;
        const graphics::TextureHandle guide_output =
            guide_target->GetColorAttachmentTexture(0);
        const bool binding_matches = binding_cache.descriptor_set.IsValid() &&
            binding_cache.pipeline == pipeline_ && binding_cache.top_level == top_level &&
            binding_cache.scene_table == reference_table &&
            binding_cache.hdr_output == hdr_output &&
            binding_cache.history_output == history_output &&
            binding_cache.guide_output == guide_output &&
            binding_cache.environment == environment.panorama.texture &&
            binding_cache.environment_sampler == environment.panorama.sampler &&
            binding_cache.camera_uniform.buffer == camera_uniform.buffer &&
            binding_cache.camera_uniform.offset == camera_uniform.offset &&
            binding_cache.camera_uniform.range == camera_uniform.range;
        if (!binding_matches)
        {
            std::vector<graphics::RayTracingResourceBinding> resource_bindings{
                graphics::RayTracingAccelerationStructureBinding{0, 0, top_level},
                graphics::RayTracingStorageTextureBinding{0, 1, hdr_output},
                graphics::RayTracingStorageTextureBinding{0, 35, history_output},
                graphics::RayTracingStorageTextureBinding{0, 36, guide_output},
                graphics::SampledTextureBinding{
                    0, 4, environment.panorama.texture,
                    environment.panorama.sampler},
                graphics::UniformBufferBinding{0, 2, camera_uniform.buffer,
                    camera_uniform.offset, camera_uniform.range},
                graphics::RayTracingBufferReferenceTableBinding{
                    0, 3, reference_table}};
            const graphics::DescriptorSetHandle new_descriptor_set =
                owner->CreateRayTracingResourceBindingSet(
                    pipeline_, {0, std::move(resource_bindings), true});
            if (!new_descriptor_set.IsValid())
                return false;
            if (binding_cache.descriptor_set.IsValid())
                owner->DestroyRayTracingResourceBindingSet(binding_cache.descriptor_set);
            binding_cache.descriptor_set = new_descriptor_set;
            binding_cache.pipeline = pipeline_;
            binding_cache.top_level = top_level;
            binding_cache.scene_table = reference_table;
            binding_cache.hdr_output = hdr_output;
            binding_cache.history_output = history_output;
            binding_cache.guide_output = guide_output;
            binding_cache.environment = environment.panorama.texture;
            binding_cache.environment_sampler = environment.panorama.sampler;
            binding_cache.camera_uniform = camera_uniform;
        }
        if (!recorder.BindRayTracingPipeline(pipeline_) ||
            !recorder.BindRayTracingResourceBindings(binding_cache.descriptor_set))
            return false;
        graphics::RayTracingDispatchDesc dispatch{
            pipeline_, binding_cache.descriptor_set,
            hdr_target->GetWidth(), hdr_target->GetHeight(), 1};
        if (fail_next_dispatch_)
        {
            fail_next_dispatch_ = false;
            dispatch.width = 0;
            const bool unexpectedly_accepted = recorder.DispatchRays(dispatch);
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Path tracing test injection submitted a zero-width dispatch; "
                   "rejected=%s",
                   unexpectedly_accepted ? "false" : "true");
            return false;
        }
        return recorder.DispatchRays(dispatch);
    }

    void PathTracingPass::ReleaseBindings(graphics::RenderBackend &backend)
    {
        ReleaseBindings(backend.GetRayTracingResourceOwner());
    }

    void PathTracingPass::ReleaseBindings(graphics::RayTracingResourceOwner *owner)
    {
        if (owner != nullptr)
        {
            for (auto &frame_bindings : bindings_)
                for (BindingCache &cache : frame_bindings)
                {
                    if (cache.descriptor_set.IsValid())
                        owner->DestroyRayTracingResourceBindingSet(cache.descriptor_set);
                    cache = {};
                }
        }
        bindings_.clear();
    }

    void PathTracingPass::Cleanup(graphics::RenderBackend &backend)
    {
        ReleaseBindings(backend);
        for (auto &target : history_targets_)
        {
            if (target)
                target->Cleanup();
            target.reset();
        }
        if (guide_target_)
            guide_target_->Cleanup();
        guide_target_.reset();
        graphics::RayTracingResourceOwner *const owner =
            backend.GetRayTracingResourceOwner();
        if (owner != nullptr && pipeline_.IsValid())
            owner->DestroyRayTracingPipeline(pipeline_);
        pipeline_ = {};
        shader_signature_ = 0;
        sample_count_ = 0;
        write_index_ = 0;
        history_signature_ = 0;
        active_ = false;
        fail_next_dispatch_ = false;
        scene_capacity_report_signature_ = 0;
    }

    RenderTarget *PathTracingPass::HistoryTarget(uint32_t index) noexcept
    {
        return index < history_targets_.size() ? history_targets_[index].get() : nullptr;
    }

    const RenderTarget *PathTracingPass::HistoryTarget(uint32_t index) const noexcept
    {
        return index < history_targets_.size() ? history_targets_[index].get() : nullptr;
    }

    uint32_t PathTracingPass::GetHistoryTargetCount() const noexcept
    {
        return static_cast<uint32_t>(std::count_if(
            history_targets_.begin(), history_targets_.end(),
            [](const auto &target) { return static_cast<bool>(target); }));
    }
}
