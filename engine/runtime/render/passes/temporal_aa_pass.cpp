#include "temporal_aa_pass.h"

#include <algorithm>

#include "asset/shader.h"
#include "asset/shader_program.h"
#include "render/frame_context.h"
#include "render/passes/fullscreen_pass_resources.h"
#include "render/prepared_render_asset_catalog.h"

namespace kpengine::render
{
    bool TemporalAaPass::PrepareResources(
        graphics::RenderBackend &backend, const PreparedRenderAssetCatalog &assets)
    {
        const auto program = assets.Get<asset::ShaderProgramResource>(
            assets.GetBuiltIn(BuiltInRenderAsset::TemporalAaProgram));
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

        if (pipeline_.IsValid() && vertex_artifact_ == vertex->data &&
            fragment_artifact_ == fragment->data)
            return true;

        auto desc = FullscreenPassResources::DescribePipeline(*vertex->data, *fragment->data);
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA16F,
                                         TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                                         TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM};
        desc.descriptor_binding_descs = {{
            {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
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
        }};
        const auto replacement = backend.CreatePipelineResource(desc);
        if (!replacement.IsValid()) return false;
        if (pipeline_.IsValid())
        {
            backend.WaitIdle();
            backend.DestroyPipelineResource(pipeline_);
            Invalidate("shader_reloaded");
        }
        pipeline_ = replacement;
        vertex_artifact_ = vertex->data;
        fragment_artifact_ = fragment->data;
        return true;
    }

    bool TemporalAaPass::EnsureHistoryTargets(
        graphics::RenderBackend &backend, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return false;
        if (history_targets_[0] && history_targets_[1] &&
            history_targets_[0]->IsValid() && history_targets_[1]->IsValid() &&
            history_targets_[0]->GetWidth() == width &&
            history_targets_[0]->GetHeight() == height)
            return true;

        if (history_targets_[0] || history_targets_[1])
            backend.WaitIdle();
        Invalidate("extent_changed");
        graphics::RenderTargetDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.color_attachments = {
            {graphics::RenderTargetColorAttachment{
                TextureFormat::TEXTURE_FORMAT_RGBA16F,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store, {0.f, 0.f, 0.f, 0.f}}},
            {graphics::RenderTargetColorAttachment{
                TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store, {0.f, 0.f, 0.f, 0.f}}},
            {graphics::RenderTargetColorAttachment{
                TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store, {0.f, 0.f, 0.f, 0.f}}},
        };
        for (auto &target : history_targets_)
        {
            if (!target)
                target = std::make_unique<RenderTarget>();
            target->Initialize(backend, desc);
            if (!target->IsValid())
            {
                for (auto &created : history_targets_)
                    if (created)
                        created->Cleanup();
                Invalidate("history_allocation_failed");
                return false;
            }
        }
        read_index_ = 0;
        return true;
    }

    void TemporalAaPass::Cleanup(graphics::RenderBackend &backend)
    {
        if (pipeline_.IsValid())
            backend.DestroyPipelineResource(pipeline_);
        pipeline_ = {};
        vertex_artifact_.reset();
        fragment_artifact_.reset();
        for (auto &target : history_targets_)
        {
            if (target)
                target->Cleanup();
            target.reset();
        }
        read_index_ = 0;
        pending_commit_ = false;
        Invalidate("uninitialized");
        history_signature_ = 0;
    }

    void TemporalAaPass::Invalidate(const char *reason) noexcept
    {
        if (history_valid_ || pending_commit_) ++reset_count_;
        history_valid_ = false;
        pending_commit_ = false;
        reset_reason_ = reason != nullptr ? reason : "explicit_reset";
    }

    void TemporalAaPass::UpdateHistorySignature(uint64_t signature) noexcept
    {
        if (history_signature_ == 0)
        {
            history_signature_ = signature;
            return;
        }
        if (signature != history_signature_)
        {
            history_signature_ = signature;
            Invalidate("scene_material_or_lighting_change");
        }
    }

    void TemporalAaPass::CommitSubmittedFrame(bool submitted) noexcept
    {
        if (!submitted || !pending_commit_)
        {
            pending_commit_ = false;
            return;
        }
        read_index_ = WriteIndex();
        history_valid_ = true;
        pending_commit_ = false;
        reset_reason_ = "none";
    }

    void TemporalAaPass::SetSettings(TemporalAaSettings settings) noexcept
    {
        settings.history_weight = std::clamp(settings.history_weight, 0.0f, 0.95f);
        if (settings.enabled != settings_.enabled)
            Invalidate(settings.enabled ? "enabled" : "disabled");
        settings_ = settings;
    }

    bool TemporalAaPass::Record(
        FrameContext &frame_context, RenderTarget &scene_hdr, RenderTarget &gbuffer,
        const FullscreenPassResources &fullscreen, graphics::CommandRecorder &recorder,
        bool camera_history_valid, Vector2f current_jitter)
    {
        RenderTarget *const history = HistoryTarget(ReadIndex());
        RenderTarget *const output = HistoryTarget(WriteIndex());
        if (!pipeline_.IsValid() || !scene_hdr.IsValid() || !gbuffer.IsValid() ||
            history == nullptr || output == nullptr || !history->IsValid() ||
            !output->IsValid() || !fullscreen.Mesh().IsValid() ||
            !fullscreen.LinearSampler().IsValid())
            return false;
        const Vector4f options{history_valid_ && camera_history_valid ? 1.0f : 0.0f,
                               settings_.history_weight,
                               current_jitter.x_, current_jitter.y_};
        const UniformAllocation allocation = frame_context.AllocateUniform(options);
        if (!allocation.IsValid())
            return false;
        const graphics::DescriptorSetHandle bindings = frame_context.AllocateResourceBindingSet(
            pipeline_, {0, {
                graphics::SampledTextureBinding{0, 0,
                    scene_hdr.GetColorAttachmentTexture(0), fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 1,
                    gbuffer.GetColorAttachmentTexture(4), fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 2,
                    gbuffer.GetColorAttachmentTexture(1), fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 3,
                    history->GetColorAttachmentTexture(0), fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 4,
                    history->GetColorAttachmentTexture(1), fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 5,
                    gbuffer.GetColorAttachmentTexture(5), fullscreen.LinearSampler()},
                graphics::SampledTextureBinding{0, 6,
                    history->GetColorAttachmentTexture(2), fullscreen.LinearSampler()},
                graphics::UniformBufferBinding{0, 7, allocation.buffer,
                    allocation.offset, allocation.range},
            }});
        if (!bindings.IsValid())
            return false;
        recorder.BindPipeline(pipeline_);
        recorder.BindMesh(fullscreen.Mesh());
        recorder.BindResourceBindings(pipeline_, bindings);
        recorder.DrawIndexed();
        pending_commit_ = true;
        return true;
    }

    RenderTarget *TemporalAaPass::HistoryTarget(uint32_t index) noexcept
    {
        return index < history_targets_.size() ? history_targets_[index].get() : nullptr;
    }

    TextureFootprint TemporalAaPass::HistoryFootprint() const
    {
        TextureFootprint footprint;
        for (const auto &target : history_targets_)
        {
            if (!target || !target->IsValid())
                return {0, std::nullopt};
            footprint += target->GetColorFootprint();
        }
        return footprint;
    }
}
