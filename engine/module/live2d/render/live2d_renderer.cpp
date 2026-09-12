#include "live2d_renderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "asset/asset_manager.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "config/path.h"
#include "graphics/backend/common/render_backend.h"
#include "render/frame_context.h"
#include "render/render_submission_executor.h"
#include "resource/resource_pipeline.h"
#include "runtime/live2d_model_instance.h"
#include "runtime/live2d_system.h"

namespace kpengine::live2d
{
    namespace
    {
        constexpr uint32_t kPreviewWidth = 720u;
        constexpr uint32_t kPreviewHeight = 960u;
        constexpr uint32_t kPositionBinding = 0u;
        constexpr uint32_t kUvBinding = 1u;
        constexpr uint32_t kConstantsBinding = 0u;
        constexpr uint32_t kTextureBinding = 1u;
        constexpr uint32_t kMaskAtlasBinding = 2u;
        constexpr float kViewerCharacterWidthScale = 1.25f;

        asset::ShaderProgramResource *LoadProgram(
            resource::ResourcePipeline &pipeline, const char *file,
            std::string &diagnostic)
        {
            asset::AssetManager &manager = asset::AssetManager::GetInstance();
            const asset::AssetID id = manager.LoadSync(GetShaderDirectory() + file);
            const auto program = manager.GetResource<asset::ShaderProgramResource>(id);
            if (!id.IsValid() || !program)
            {
                diagnostic = std::string("Live2D shader program failed to load: ") + file;
                return nullptr;
            }
            pipeline.ProcessShader(program->GatherShaders(asset::ShaderProgramVariant::Bound));
            return program.get();
        }

        asset::ShaderData *GetStage(asset::ShaderProgramResource *program,
                                    ShaderStage stage)
        {
            if (program == nullptr)
            {
                return nullptr;
            }
            const auto shader = program->GetShader(stage, ShaderFormat::SHADER_FORMAT_GLSL,
                                                   asset::ShaderProgramVariant::Bound);
            return shader && shader->data ? shader->data.get() : nullptr;
        }

        std::array<float, 16> FitTransform(const Live2DFrameSnapshot &snapshot,
                                            const uint32_t width,
                                            const uint32_t height)
        {
            Live2DVector2 minimum{std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max()};
            Live2DVector2 maximum{std::numeric_limits<float>::lowest(),
                                  std::numeric_limits<float>::lowest()};
            bool has_position = false;
            for (const Live2DVector2 position : snapshot.positions)
            {
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
                maximum.x = std::max(maximum.x, position.x);
                maximum.y = std::max(maximum.y, position.y);
                has_position = true;
            }
            if (!has_position)
            {
                return {1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f};
            }
            const float center_x = (minimum.x + maximum.x) * 0.5f;
            const float center_y = (minimum.y + maximum.y) * 0.5f;
            const float extent_x = std::max(maximum.x - minimum.x, 1.0e-4f);
            const float extent_y = std::max(maximum.y - minimum.y, 1.0e-4f);
            const float aspect = static_cast<float>(width) /
                                 static_cast<float>(height);
            const float scale = std::min(1.75f / extent_y,
                                         1.75f * aspect / extent_x);
            return {scale * kViewerCharacterWidthScale, 0.0f, 0.0f, 0.0f,
                    0.0f, scale, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    -center_x * scale * kViewerCharacterWidthScale,
                    -center_y * scale, 0.0f, 1.0f};
        }
    }

    Live2DRenderer::Live2DRenderer(Live2DSystem &system,
                                   const asset::AssetID model_asset)
        : system_(&system), model_asset_(model_asset)
    {
    }

    Live2DRenderer::~Live2DRenderer()
    {
        Cleanup();
    }

    bool Live2DRenderer::Initialize(graphics::RenderBackend &backend,
                                    const uint32_t width, const uint32_t height,
                                    std::string &diagnostic)
    {
        Cleanup();
        backend_ = &backend;
        proxy_.output_to_presentation = presentation_target_requested_;
        // The model textures are sRGB, so the shader works in linear space and
        // the target's format owns the transfer function. An UNORM target would
        // store linear bytes and read back visibly darker than an sRGB one.
        output_color_format_ = presentation_target_requested_
                                   ? backend.GetPresentationColorFormat()
                                   : TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        if (system_ == nullptr || !model_asset_.IsValid())
        {
            diagnostic = "Live2D renderer has no model asset";
            return false;
        }
        instance_ = system_->CreateInstance(model_asset_);
        if (!instance_ || !instance_->IsValid())
        {
            diagnostic = "Live2D renderer could not create the Hiyori model instance";
            Cleanup();
            return false;
        }
        if (!instance_->ExtractStaticData(static_data_, diagnostic))
        {
            Cleanup();
            return false;
        }
        if (!static_data_.feature_report.IsSupported())
        {
            diagnostic = ValidateLive2DRenderFeatureReport(
                              static_data_.feature_report)
                              .diagnostic;
            Cleanup();
            return false;
        }

        graphics::RenderTargetDesc mask_desc{};
        mask_desc.width = kLive2DMaskAtlasWidth;
        mask_desc.height = kLive2DMaskAtlasHeight;
        mask_desc.color_attachments = {{graphics::RenderTargetColorAttachment{
            TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
            graphics::RenderTargetLoadOp::Clear,
            graphics::RenderTargetStoreOp::Store,
            {1.0f, 1.0f, 1.0f, 1.0f}}}};
        proxy_.mask_atlas_target = backend.CreateRenderTarget(mask_desc);
        proxy_.mask_atlas_texture = backend.GetRenderTargetColor(proxy_.mask_atlas_target);
        if (!proxy_.mask_atlas_target.IsValid() || !proxy_.mask_atlas_texture.IsValid())
        {
            diagnostic = "Live2D renderer could not create preview render targets";
            Cleanup();
            return false;
        }
        if (!ResizeOutput(width == 0u ? kPreviewWidth : width,
                          height == 0u ? kPreviewHeight : height, diagnostic))
        {
            Cleanup();
            return false;
        }

        if (!CreateShadersAndPipelines(diagnostic) ||
            !CreateGeometryAndTextures(diagnostic))
        {
            Cleanup();
            return false;
        }
        initialized_ = true;
        return true;
    }

    bool Live2DRenderer::ResizeOutput(const uint32_t width, const uint32_t height,
                                      std::string &diagnostic)
    {
        diagnostic.clear();
        if (backend_ == nullptr)
        {
            diagnostic = "Live2D renderer has no graphics backend";
            return false;
        }
        if (width == 0u || height == 0u)
        {
            diagnostic = "Live2D output extent must be non-zero";
            return false;
        }
        if (proxy_.output_target.IsValid() && proxy_.output_width == width &&
            proxy_.output_height == height)
        {
            return true;
        }

        // Build the replacement first. A failed create must leave the previous
        // target and view untouched so the caller can keep recording into the
        // last good result instead of losing its output.
        graphics::RenderTargetDesc output_desc{};
        output_desc.width = width;
        output_desc.height = height;
        graphics::RenderTargetColorAttachment output_attachment{};
        output_attachment.format = output_color_format_;
        output_attachment.load_op = graphics::RenderTargetLoadOp::Clear;
        output_attachment.store_op = graphics::RenderTargetStoreOp::Store;
        // Keep the viewer backdrop opaque so the result remains visible when
        // the output is presented or exported as an image.
        output_attachment.clear_color = output_clear_color_;
        output_desc.color_attachments = {{output_attachment}};
        const graphics::RenderTargetHandle replacement =
            backend_->CreateRenderTarget(output_desc);
        if (!replacement.IsValid())
        {
            diagnostic = "Live2D renderer could not create the replacement render target";
            return false;
        }
        const graphics::RenderTargetView replacement_view =
            backend_->GetRenderTargetView(replacement);
        if (!replacement_view.IsValid())
        {
            backend_->DestroyRenderTarget(replacement);
            diagnostic = "Live2D renderer could not resolve the replacement render target view";
            return false;
        }

        // The host calls this at the beginning of a frame, before recording any
        // Live2D work, so the wait only has to cover readback and previously
        // submitted draws that still reference the old target.
        backend_->WaitIdle();
        if (proxy_.output_target.IsValid())
        {
            backend_->DestroyRenderTarget(proxy_.output_target);
        }
        proxy_.output_target = replacement;
        output_view_ = replacement_view;
        proxy_.output_width = width;
        proxy_.output_height = height;
        return true;
    }

    bool Live2DRenderer::CreateShadersAndPipelines(std::string &diagnostic)
    {
        resource::ResourcePipeline pipeline;
        pipeline.Initialize({backend_->GetGraphicsAPI()});
        asset::ShaderProgramResource *color = LoadProgram(
            pipeline, "live2d_color.shader", diagnostic);
        asset::ShaderProgramResource *masked = LoadProgram(
            pipeline, "live2d_masked.shader", diagnostic);
        asset::ShaderProgramResource *mask = LoadProgram(
            pipeline, "live2d_mask.shader", diagnostic);
        if (color == nullptr || masked == nullptr || mask == nullptr)
        {
            return false;
        }

        const std::array<graphics::VertexBindingDesc, 2> bindings{{
            {kPositionBinding, sizeof(Live2DVector2), false},
            {kUvBinding, sizeof(Live2DVector2), false}}};
        const std::array<graphics::VertexAttributionDesc, 2> attributes{{
            {0u, kPositionBinding, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, 0u},
            {1u, kUvBinding, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, 0u}}};
        const std::array<graphics::DescriptorBindingDesc, 2> color_descriptors{{
            {kConstantsBinding, 1u, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             static_cast<ShaderStage>(static_cast<uint32_t>(ShaderStage::SHADER_STAGE_VERTEX) |
                                      static_cast<uint32_t>(ShaderStage::SHADER_STAGE_FRAGMENT))},
            {kTextureBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT}}};
        const std::array<graphics::DescriptorBindingDesc, 3> masked_descriptors{{
            {kConstantsBinding, 1u, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             static_cast<ShaderStage>(static_cast<uint32_t>(ShaderStage::SHADER_STAGE_VERTEX) |
                                      static_cast<uint32_t>(ShaderStage::SHADER_STAGE_FRAGMENT))},
            {kTextureBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {kMaskAtlasBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT}}};
        const std::array<graphics::DescriptorBindingDesc, 2> mask_descriptors{{
            {kConstantsBinding, 1u, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             static_cast<ShaderStage>(static_cast<uint32_t>(ShaderStage::SHADER_STAGE_VERTEX) |
                                      static_cast<uint32_t>(ShaderStage::SHADER_STAGE_FRAGMENT))},
            {kTextureBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT}}};

        const auto make_desc = [&](asset::ShaderProgramResource *program,
                                   const bool masked_pipeline,
                                   const bool mask_source,
                                   const bool culling,
                                   const Live2DBlendMode blend_mode)
        {
            graphics::PipelineDesc desc{};
            desc.vert_shader = GetStage(program, ShaderStage::SHADER_STAGE_VERTEX);
            desc.frag_shader = GetStage(program, ShaderStage::SHADER_STAGE_FRAGMENT);
            desc.binding_descs.assign(bindings.begin(), bindings.end());
            desc.attri_descs.assign(attributes.begin(), attributes.end());
            desc.raster_state.cull_mode = culling
                                              ? graphics::CullMode::CULL_MODE_BACK
                                              : graphics::CullMode::CULL_MODE_NONE;
            desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
            desc.color_attachment_formats = {mask_source
                                                  ? TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM
                                                  : output_color_format_};
            if (mask_source)
            {
                desc.blend_attachment_state.blend_enabled = true;
                desc.blend_attachment_state.src_color_blend_factor =
                    graphics::BlendFactor::BLEND_FACTOR_ZERO;
                desc.blend_attachment_state.dst_color_blend_factor =
                    graphics::BlendFactor::BLEND_FACTOR_SRC_COLOR;
                desc.blend_attachment_state.src_alpha_blend_factor =
                    graphics::BlendFactor::BLEND_FACTOR_ZERO;
                desc.blend_attachment_state.dst_alpha_blend_factor =
                    graphics::BlendFactor::BLEND_FACTOR_SRC_COLOR;
            }
            else
            {
                desc.blend_attachment_state = BuildLive2DBlendState(blend_mode);
            }
            if (masked_pipeline)
            {
                desc.descriptor_binding_descs = {
                    std::vector<graphics::DescriptorBindingDesc>(
                        masked_descriptors.begin(), masked_descriptors.end())};
            }
            else if (mask_source)
            {
                desc.descriptor_binding_descs = {
                    std::vector<graphics::DescriptorBindingDesc>(
                        mask_descriptors.begin(), mask_descriptors.end())};
            }
            else
            {
                desc.descriptor_binding_descs = {
                    std::vector<graphics::DescriptorBindingDesc>(
                        color_descriptors.begin(), color_descriptors.end())};
            }
            return desc;
        };

        const auto create = [&](asset::ShaderProgramResource *program,
                                const bool masked_pipeline,
                                const bool mask_source,
                                const bool culling,
                                const Live2DBlendMode blend_mode)
        {
            graphics::PipelineHandle handle = backend_->CreatePipelineResource(
                make_desc(program, masked_pipeline, mask_source, culling, blend_mode));
            if (!handle.IsValid())
            {
                diagnostic = "Live2D pipeline creation failed";
                return graphics::PipelineHandle{};
            }
            pipelines_.push_back(handle);
            return handle;
        };

        resources_.normal_culled = create(color, false, false, true, Live2DBlendMode::Normal);
        resources_.normal_unculled = create(color, false, false, false, Live2DBlendMode::Normal);
        resources_.additive_culled = create(color, false, false, true, Live2DBlendMode::Additive);
        resources_.additive_unculled = create(color, false, false, false, Live2DBlendMode::Additive);
        resources_.multiplicative_culled = create(color, false, false, true, Live2DBlendMode::Multiplicative);
        resources_.multiplicative_unculled = create(color, false, false, false, Live2DBlendMode::Multiplicative);
        resources_.masked_normal_culled = create(masked, true, false, true, Live2DBlendMode::Normal);
        resources_.masked_normal_unculled = create(masked, true, false, false, Live2DBlendMode::Normal);
        resources_.masked_additive_culled = create(masked, true, false, true, Live2DBlendMode::Additive);
        resources_.masked_additive_unculled = create(masked, true, false, false, Live2DBlendMode::Additive);
        resources_.masked_multiplicative_culled = create(masked, true, false, true, Live2DBlendMode::Multiplicative);
        resources_.masked_multiplicative_unculled = create(masked, true, false, false, Live2DBlendMode::Multiplicative);
        resources_.mask_culled = create(mask, false, true, true, Live2DBlendMode::Normal);
        resources_.mask_unculled = create(mask, false, true, false, Live2DBlendMode::Normal);
        graphics::SamplerSettings sampler_settings{};
        sampler_settings.address_mode_u =
            graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_settings.address_mode_v =
            graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_settings.address_mode_w =
            graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_settings.mipmap_mode =
            graphics::SamplerMipmapMode::SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_settings.enable_anisotropy = false;
        sampler_settings.max_lod = 0.0f;
        resources_.sampler = backend_->CreateSampler(sampler_settings);
        return resources_.sampler.IsValid() && diagnostic.empty();
    }

    bool Live2DRenderer::CreateGeometryAndTextures(std::string &diagnostic)
    {
        proxy_.position_buffer = backend_->CreateBuffer(
            {graphics::BufferRole::Vertex, graphics::BufferUpdateMode::PerFrame,
             static_data_.uvs.size() * sizeof(Live2DVector2)}, nullptr, 0u);
        proxy_.uv_buffer = backend_->CreateBuffer(
            {graphics::BufferRole::Vertex, graphics::BufferUpdateMode::Immutable,
             static_data_.uvs.size() * sizeof(Live2DVector2)},
            static_data_.uvs.data(), static_data_.uvs.size() * sizeof(Live2DVector2));
        proxy_.index_buffer = backend_->CreateBuffer(
            {graphics::BufferRole::Index, graphics::BufferUpdateMode::Immutable,
             static_data_.indices.size() * sizeof(std::uint16_t)},
            static_data_.indices.data(), static_data_.indices.size() * sizeof(std::uint16_t));
        if (!proxy_.position_buffer.IsValid() || !proxy_.uv_buffer.IsValid() ||
            !proxy_.index_buffer.IsValid())
        {
            diagnostic = "Live2D geometry buffer creation failed";
            return false;
        }

        const auto &dependencies = instance_->TextureDependencies();
        if (dependencies.size() != static_data_.texture_count)
        {
            diagnostic = "Live2D texture dependency count does not match static data";
            return false;
        }
        textures_.reserve(dependencies.size());
        for (std::size_t dependency_index = 0u;
             dependency_index < dependencies.size(); ++dependency_index)
        {
            const auto &dependency = dependencies[dependency_index];
            if (!dependency || !dependency->data)
            {
                diagnostic = "Live2D texture dependency is unavailable";
                return false;
            }
            graphics::TextureSettings settings{};
            settings.format = dependency->data->format;
            settings.mip_levels = dependency->data->GetMipLevelCount();
            const graphics::TextureHandle texture = backend_->CreateTexture(
                *dependency->data, settings);
            if (!texture.IsValid())
            {
                diagnostic = "Live2D texture upload failed";
                return false;
            }
            textures_.push_back(texture);
        }
        proxy_.textures = textures_;
        proxy_.static_data = static_data_;
        return true;
    }

    bool Live2DRenderer::Record(render::FrameContext &frame_context,
                                graphics::CommandRecorder &recorder,
                                const float delta_time,
                                std::string &diagnostic)
    {
        (void)delta_time;
        if (!initialized_ || !instance_)
        {
            diagnostic = "Live2D renderer is not initialized";
            return false;
        }
        if (!instance_->Update())
        {
            diagnostic = "Live2D model update failed";
            return false;
        }
        Live2DFrameSnapshot snapshot;
        if (!instance_->ExtractFrameSnapshot(snapshot, diagnostic))
        {
            return false;
        }
        // Extraction increments the sequence, so a repeated or out-of-order
        // snapshot means the caller reused stale state.
        if (has_last_frame_sequence_ &&
            snapshot.frame_sequence <= last_frame_sequence_)
        {
            diagnostic =
                "Live2D snapshot frame sequence repeated or moved backwards";
            return false;
        }
        Live2DRenderPlanOptions options{};
        options.model_transform = FitTransform(snapshot, proxy_.output_width,
                                               proxy_.output_height);
        options.viewport = {0.0f, 0.0f,
                            static_cast<float>(proxy_.output_width),
                            static_cast<float>(proxy_.output_height), 0.0f, 1.0f};
        const Live2DRenderPlanResult plan = Live2DRenderPlanner::Plan(
            resources_, proxy_, static_data_, snapshot, options);
        if (!plan.succeeded)
        {
            diagnostic = plan.diagnostic;
            return false;
        }
        render::RenderSubmission submission = plan.submission.work;
        for (render::SubmissionPass &pass : submission.passes)
        {
            if (pass.presentation)
            {
                pass.clear_color = background_color_;
            }
        }
        const render::RenderSubmissionExecutionResult execution =
            render::RenderSubmissionExecutor::Execute(submission, frame_context, recorder);
        if (!execution.succeeded)
        {
            // A partial result is already closed but incomplete; the host must
            // not present or capture it.
            diagnostic = execution.partial_output
                             ? execution.diagnostic +
                                   " (frame holds a partial Live2D output)"
                             : execution.diagnostic;
            return false;
        }
        last_frame_sequence_ = snapshot.frame_sequence;
        has_last_frame_sequence_ = true;
        last_counters_ = plan.submission.counters;
        return true;
    }

    graphics::RenderTargetView Live2DRenderer::GetOutputView() const
    {
        return output_view_;
    }

    std::uint32_t Live2DRenderer::GetLiveGpuHandleCount() const noexcept
    {
        std::uint32_t count = 0u;
        for (const graphics::PipelineHandle handle : pipelines_)
        {
            count += handle.IsValid() ? 1u : 0u;
        }
        for (const graphics::TextureHandle handle : textures_)
        {
            count += handle.IsValid() ? 1u : 0u;
        }
        for (const graphics::BufferHandle handle :
             {proxy_.position_buffer, proxy_.uv_buffer, proxy_.index_buffer})
        {
            count += handle.IsValid() ? 1u : 0u;
        }
        count += resources_.sampler.IsValid() ? 1u : 0u;
        count += proxy_.mask_atlas_target.IsValid() ? 1u : 0u;
        count += proxy_.output_target.IsValid() ? 1u : 0u;
        return count;
    }

    void Live2DRenderer::DestroyPipelines() noexcept
    {
        if (backend_ == nullptr)
        {
            pipelines_.clear();
            return;
        }
        for (const graphics::PipelineHandle handle : pipelines_)
        {
            if (handle.IsValid())
            {
                backend_->DestroyPipelineResource(handle);
            }
        }
        pipelines_.clear();
        resources_ = {};
    }

    void Live2DRenderer::DestroyGeometryAndTextures() noexcept
    {
        if (backend_ == nullptr)
        {
            textures_.clear();
            proxy_.position_buffer = {};
            proxy_.uv_buffer = {};
            proxy_.index_buffer = {};
            return;
        }
        for (const graphics::TextureHandle texture : textures_)
        {
            if (texture.IsValid())
            {
                backend_->DestroyTexture(texture);
            }
        }
        textures_.clear();
        for (const graphics::BufferHandle buffer :
             {proxy_.position_buffer, proxy_.uv_buffer, proxy_.index_buffer})
        {
            if (buffer.IsValid())
            {
                backend_->DestroyBufferResource(buffer);
            }
        }
        proxy_.position_buffer = {};
        proxy_.uv_buffer = {};
        proxy_.index_buffer = {};
        proxy_.textures.clear();
    }

    void Live2DRenderer::DestroyRenderTargets() noexcept
    {
        if (backend_ != nullptr)
        {
            if (proxy_.output_target.IsValid())
            {
                backend_->DestroyRenderTarget(proxy_.output_target);
            }
            if (proxy_.mask_atlas_target.IsValid())
            {
                backend_->DestroyRenderTarget(proxy_.mask_atlas_target);
            }
        }
        proxy_.output_target = {};
        proxy_.mask_atlas_target = {};
        proxy_.mask_atlas_texture = {};
        proxy_.output_width = 0u;
        proxy_.output_height = 0u;
        output_view_ = {};
    }

    void Live2DRenderer::Cleanup() noexcept
    {
        // Release in reverse creation order: geometry/textures, then the
        // sampler and pipelines that consume them, then the render targets.
        DestroyGeometryAndTextures();
        if (backend_ != nullptr && resources_.sampler.IsValid())
        {
            backend_->DestroySampler(resources_.sampler);
        }
        resources_.sampler = {};
        DestroyPipelines();
        DestroyRenderTargets();
        proxy_ = {};
        static_data_ = {};
        last_counters_ = {};
        last_frame_sequence_ = 0u;
        has_last_frame_sequence_ = false;
        instance_.reset();
        backend_ = nullptr;
        initialized_ = false;
    }
}
